// ===================================================================
// RFL Lemon Grammar — Combined
// ===================================================================

%name RflParse
%token_prefix TOK_
%extra_context { RflParserContext* ctx }
%default_destructor { (void)ctx; }

%include {
#include "rfl_parser_impl.hpp"
#include "rfl_token.hpp"
#include <cassert>
#include <string>
#include <vector>
#include <memory>
#include <optional>
#include <sstream>
#include <algorithm>
#include <cctype>

// Helper: strip surrounding quotes from a string_view
static std::string strip_quotes(std::string_view sv) {
    if (sv.size() >= 2 && (sv.front() == '"' || sv.front() == '\'')) {
        return std::string(sv.substr(1, sv.size() - 2));
    }
    return std::string(sv);
}

static bool has_schema_extension(std::string const& path) {
    std::string lower;
    lower.reserve(path.size());
    for (char ch : path) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return lower.size() >= 7 && lower.compare(lower.size() - 7, 7, ".schema") == 0;
}

static bool is_schema_import_qualifier(std::string const& qualifier) {
    std::string lower;
    lower.reserve(qualifier.size());
    for (char ch : qualifier) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return lower == "schema";
}

static void append_schema_import(RflParserContext* ctx, RflLemonToken const& path_token) {
    std::string path = strip_quotes(path_token.as_sv());
    if (has_schema_extension(path)) {
        ctx->state.schema_imports.push_back(
            {.path = std::move(path),
             .source_name = ctx->source_name,
             .line = path_token.line,
             .column = path_token.column});
    } else {
        ctx->add_error(path_token.line, path_token.column,
            "Unsupported schema import '" + path + "'. Expected a .schema file.");
    }
}

// Helper struct for generic type parsing (e.g., List<String>, Map<String, int>)
struct GenericType {
    std::string base;
    std::vector<std::unique_ptr<GenericType>> params;

    GenericType() = default;
    explicit GenericType(std::string b) : base(std::move(b)) {}
};

// Helper: convert GenericType to ParsedField
static ParsedField convert_generic_to_field(std::string const& name, GenericType* gt) {
    ParsedField field;
    field.name = name;
    field.type = parse_field_type(gt->base);
    // Align declaration parser with decision-table compiler behavior:
    // custom types are treated as Object with a custom type tag.
    if (field.type == FT_Unknown) {
        field.type = FT_Object;
        field.type_params.emplace_back(FT_Object, gt->base);
    }

    // Convert type parameters
    for (auto& param : gt->params) {
        TypeParameter tp;
        tp.base_type = parse_field_type(param->base);
        if (tp.base_type == FT_Unknown || tp.base_type == FT_Object) {
            tp.custom_type = param->base;
            tp.base_type = FT_Object;
        }
        // Handle nested generics (e.g., List<List<int>>)
        if (!param->params.empty()) {
            auto nested_gt = param->params[0].get();
            tp.nested = std::make_unique<TypeParameter>();
            tp.nested->base_type = parse_field_type(nested_gt->base);
            if (tp.nested->base_type == FT_Unknown || tp.nested->base_type == FT_Object) {
                tp.nested->custom_type = nested_gt->base;
                tp.nested->base_type = FT_Object;
            }
        }
        field.type_params.push_back(std::move(tp));
    }

    return field;
}

// Helper: parse integer from string_view
static long long parse_int(std::string_view sv) {
    try { return std::stoll(std::string(sv)); }
    catch (...) { return 0; }
}

// Helper: parse double from string_view
static double parse_dbl(std::string_view sv) {
    try { return std::stod(std::string(sv)); }
    catch (...) { return 0.0; }
}

// Helper: parse duration string to milliseconds
static long long parse_duration_ms(std::string_view sv) {
    std::string s(sv);
    size_t split = s.find_first_not_of("0123456789-");
    if (split == std::string::npos) return parse_int(sv);
    long long val = parse_int(s.substr(0, split));
    std::string unit = s.substr(split);
    if (unit == "h") return val * 3600000;
    if (unit == "m") return val * 60000;
    if (unit == "s") return val * 1000;
    return val; // "ms"
}

// Helper: normalize indentation of RHS code
static void normalize_indent(std::string& code) {
    std::istringstream stream(code);
    std::string line;
    std::vector<std::string> lines;
    while (std::getline(stream, line)) lines.push_back(line);
    if (lines.empty()) { code.clear(); return; }
    auto first = std::find_if(lines.begin(), lines.end(),
        [](auto const& s) { return s.find_first_not_of(" \t\r\n") != std::string::npos; });
    if (first == lines.end()) { code.clear(); return; }
    auto last = std::find_if(lines.rbegin(), lines.rend(),
        [](auto const& s) { return s.find_first_not_of(" \t\r\n") != std::string::npos; });
    std::vector<std::string> trimmed(first, last.base());
    size_t min_indent = std::string::npos;
    for (auto const& l : trimmed) {
        size_t fc = l.find_first_not_of(" \t");
        if (fc != std::string::npos && (min_indent == std::string::npos || fc < min_indent))
            min_indent = fc;
    }
    if (min_indent == 0 || min_indent == std::string::npos) {
        std::ostringstream oss;
        for (size_t i = 0; i < trimmed.size(); ++i) {
            oss << trimmed[i]; if (i < trimmed.size() - 1) oss << "\n";
        }
        code = oss.str(); return;
    }
    std::ostringstream oss;
    for (size_t i = 0; i < trimmed.size(); ++i) {
        auto const& l = trimmed[i];
        if (l.find_first_not_of(" \t") != std::string::npos && l.length() >= min_indent)
            oss << l.substr(min_indent);
        else oss << l;
        if (i < trimmed.size() - 1) oss << "\n";
    }
    code = oss.str();
}

// Helper: set RHS value on a ParsedConstraint from a ConstraintValue
static void set_rhs_value(ParsedConstraint& c, ConstraintValue const& v) {
    if (std::holds_alternative<std::string>(v)) {
        std::string const& s = std::get<std::string>(v);
        if (!s.empty() && s[0] == '$') {
            if (s.find_first_of("+-*/") != std::string::npos) {
                c.right_arith_expr = s;
            } else {
                size_t dot_pos = s.find('.');
                if (dot_pos != std::string::npos) {
                    c.right_bound_field = {s.substr(0, dot_pos), s.substr(dot_pos + 1)};
                } else {
                    c.right_bound_field = {s, "this"};
                }
            }
        } else {
            c.right_literal = v;
        }
    } else {
        c.right_literal = v;
    }
}

} // end %include

// Lemon token type — must be trivially constructible for C union compatibility
// We pass a plain-old-data struct; the text pointer is valid for the lifetime of the input.
%token_type { RflLemonToken }

// Non-terminal types
%type qualified_name     { std::string* }
%type qualified_name_part { std::string* }
%type pattern            { ParsedPattern* }
%type pattern_list       { std::vector<ParsedPattern>* }
%type and_block          { std::vector<ParsedPattern>* }
%type constraint_expr    { ConstraintNode* }
%type or_expr            { ConstraintNode* }
%type and_expr           { ConstraintNode* }
%type constraint_item    { ConstraintNode* }
%type primary_expr_val   { ConstraintValue* }
%type value_list         { std::vector<ConstraintValue>* }
%type opt_binding        { std::string* }
%type accumulate_src_pat { ParsedPattern* }
%type rhs_elements       { std::string* }
%type generic_type       { GenericType* }

// Destructors for heap-allocated non-terminals
%destructor qualified_name     { delete $$; }
%destructor qualified_name_part { delete $$; }
%destructor pattern            { delete $$; }
%destructor pattern_list       { delete $$; }
%destructor and_block          { delete $$; }
%destructor constraint_expr    { delete $$; }
%destructor or_expr            { delete $$; }
%destructor and_expr           { delete $$; }
%destructor constraint_item    { delete $$; }
%destructor primary_expr_val   { delete $$; }
%destructor value_list         { delete $$; }
%destructor opt_binding        { delete $$; }
%destructor accumulate_src_pat { delete $$; }
%destructor rhs_elements       { delete $$; }
%destructor generic_type       { delete $$; }

// Error handling
%syntax_error {
    if (TOKEN.text_len > 0) {
        ctx->add_error(TOKEN.line, TOKEN.column,
            std::string("Syntax error near '") + TOKEN.as_string() + "'");
    } else {
        ctx->add_error(TOKEN.line, TOKEN.column, "Unexpected end of input");
    }
}

%parse_failure {
    ctx->failed = true;
    if (ctx->errors.empty()) {
        ctx->add_error(0, 0, "Parse failed");
    }
}

// Operator precedence (lowest to highest)
%left OR_OR.
%left AND_AND COMMA.
%left EQ NE.
%left LT GT LE GE.
%left PLUS MINUS.
%left STAR SLASH.
%right BANG.

program ::= stmt_list.

stmt_list ::= .
stmt_list ::= stmt_list stmt.
stmt_list ::= stmt_list error. // Error recovery: skip bad statement

// --- Statements ---
stmt ::= package_stmt.
stmt ::= import_stmt.
stmt ::= global_stmt.
stmt ::= declare_stmt.
stmt ::= enum_stmt.
stmt ::= query_stmt.
stmt ::= function_stmt.
stmt ::= rule_stmt.

// --- Package ---
package_stmt ::= PACKAGE qualified_name(N) opt_semi. {
    ctx->state.package_name = *N;
    delete N;
}

// --- Import ---
import_stmt ::= IMPORT qualified_name(N) opt_semi. {
    ctx->state.parsed_imports.push_back(*N);
    delete N;
}
import_stmt ::= IMPORT qualified_name(N) DOTSTAR opt_semi. {
    ctx->state.parsed_imports.push_back(*N + ".*");
    delete N;
}
import_stmt ::= IMPORT IDENTIFIER(K) STRING(P) opt_semi. {
    std::string qualifier = K.as_string();
    if (is_schema_import_qualifier(qualifier)) {
        append_schema_import(ctx, P);
    } else {
        ctx->add_error(K.line, K.column,
            "Unsupported import qualifier '" + qualifier + "'. Expected 'schema'.");
    }
}
import_stmt ::= IMPORT STRING(P) opt_semi. {
    append_schema_import(ctx, P);
}

opt_semi ::= .
opt_semi ::= SEMICOLON.

// --- Global ---
global_stmt ::= GLOBAL qualified_name(T) IDENTIFIER(N) opt_semi. {
    ParsedGlobal g;
    g.type = *T;
    g.name = N.as_string();
    ctx->state.parsed_globals.push_back(std::move(g));
    delete T;
}

// --- Qualified name ---
qualified_name(A) ::= qualified_name_part(N). {
    A = N;
}
qualified_name(A) ::= qualified_name(B) DOT qualified_name_part(N). {
    A = B;
    A->append(".");
    A->append(*N);
    delete N;
}

qualified_name_part(A) ::= IDENTIFIER(N). { A = new std::string(N.as_string()); }
qualified_name_part(A) ::= TIME(T). { A = new std::string(T.as_string()); }
qualified_name_part(A) ::= LENGTH(T). { A = new std::string(T.as_string()); }
qualified_name_part(A) ::= WINDOW(T). { A = new std::string(T.as_string()); }

// --- Declare ---
declare_stmt ::= DECLARE IDENTIFIER(N) decl_opt_annotations decl_body END. {
    ctx->current_decl.type_name = N.as_string();
    ctx->current_decl.source_package = ctx->state.package_name;
    ctx->current_decl.annotations = std::move(ctx->pending_annotations);
    ctx->pending_annotations.clear();
    ctx->state.parsed_declarations.push_back(std::move(ctx->current_decl));
    ctx->current_decl = ParsedDeclaration{};
}

decl_opt_annotations ::= .
decl_opt_annotations ::= annotation_list.

decl_body ::= .
decl_body ::= decl_body field_def.
decl_body ::= decl_body field_def COMMA.

// Field definition with generic type support: name: Type or name: List<T> or name: Map<K,V>
field_def ::= IDENTIFIER(N) COLON generic_type(T). {
    ctx->current_decl.fields.push_back(convert_generic_to_field(N.as_string(), T));
    delete T;
}

// --- Generic type rules ---
// Base type: String, int, Item, etc.
generic_type(A) ::= IDENTIFIER(N). {
    A = new GenericType(N.as_string());
}

// Single parameter generic: List<T>, Set<T>
generic_type(A) ::= IDENTIFIER(N) LT generic_type(T) GT. {
    A = new GenericType(N.as_string());
    A->params.push_back(std::unique_ptr<GenericType>(T));
}

// Two parameter generic: Map<K, V>
generic_type(A) ::= IDENTIFIER(N) LT generic_type(K) COMMA generic_type(V) GT. {
    A = new GenericType(N.as_string());
    A->params.push_back(std::unique_ptr<GenericType>(K));
    A->params.push_back(std::unique_ptr<GenericType>(V));
}

// --- Enum ---
enum_stmt ::= ENUM IDENTIFIER(N) enum_body END. {
    ctx->current_enum.enum_name = N.as_string();
    ctx->current_enum.source_package = ctx->state.package_name;
    ctx->state.parsed_enums.push_back(std::move(ctx->current_enum));
    ctx->current_enum = ParsedEnum{};
}
enum_stmt ::= ENUM IDENTIFIER(N) LT IDENTIFIER(T) GT enum_body END. {
    ctx->current_enum.enum_name = N.as_string();
    ctx->current_enum.underlying_type = T.as_string();
    ctx->current_enum.source_package = ctx->state.package_name;
    ctx->state.parsed_enums.push_back(std::move(ctx->current_enum));
    ctx->current_enum = ParsedEnum{};
}

enum_body ::= .
enum_body ::= enum_body IDENTIFIER(V). {
    ctx->current_enum.values.push_back(V.as_string());
}
enum_body ::= enum_body IDENTIFIER(V) COMMA. {
    ctx->current_enum.values.push_back(V.as_string());
}

// ===================================================================
// Part 3b: Function (stub — captures but doesn't deeply parse)
// ===================================================================

function_stmt ::= FUNCTION func_sig LPAREN func_params RPAREN func_body. {
    ctx->state.parsed_functions.push_back(ParsedFunction{});
}

func_sig ::= IDENTIFIER.
func_sig ::= func_sig IDENTIFIER.

func_params ::= .
func_params ::= func_params_inner.
func_params_inner ::= IDENTIFIER.
func_params_inner ::= func_params_inner COMMA IDENTIFIER.

func_body ::= LBRACE func_body_content RBRACE.
func_body_content ::= .
func_body_content ::= func_body_content any_token_not_rbrace.
any_token_not_rbrace ::= IDENTIFIER.
any_token_not_rbrace ::= INTEGER.
any_token_not_rbrace ::= DOUBLE.
any_token_not_rbrace ::= STRING.
any_token_not_rbrace ::= VARIABLE.
any_token_not_rbrace ::= LPAREN.
any_token_not_rbrace ::= RPAREN.
any_token_not_rbrace ::= LBRACKET.
any_token_not_rbrace ::= RBRACKET.
any_token_not_rbrace ::= LBRACE func_body_content RBRACE.
any_token_not_rbrace ::= SEMICOLON.
any_token_not_rbrace ::= COMMA.
any_token_not_rbrace ::= DOT.
any_token_not_rbrace ::= COLON.
any_token_not_rbrace ::= PLUS.
any_token_not_rbrace ::= MINUS.
any_token_not_rbrace ::= STAR.
any_token_not_rbrace ::= SLASH.
any_token_not_rbrace ::= EQ.
any_token_not_rbrace ::= NE.
any_token_not_rbrace ::= LT.
any_token_not_rbrace ::= GT.
any_token_not_rbrace ::= LE.
any_token_not_rbrace ::= GE.
rule_stmt ::= opt_annotations RULE STRING(N) opt_attrs when_block THEN rhs_block END. {
    ctx->current_rule.name = strip_quotes(N.as_sv());
    ctx->current_rule.pos = {ctx->source_name, N.line, N.column};
    ctx->current_rule.annotations = std::move(ctx->pending_annotations);
    ctx->pending_annotations.clear();
    ctx->current_rule.condition_groups = std::move(ctx->current_condition_groups);
    if (ctx->current_rule.condition_groups.empty()) {
        ctx->current_rule.condition_groups.emplace_back();
    }
    ctx->current_rule.source_package = ctx->state.package_name;
    ctx->current_rule.source_imports = ctx->state.parsed_imports;
    ctx->state.parsed_rules.push_back(std::move(ctx->current_rule));
    ctx->current_rule = ParsedRule{};
    ctx->current_condition_groups.clear();
}

// --- Annotations ---
opt_annotations ::= .
opt_annotations ::= annotation_list.

annotation_list ::= annotation.
annotation_list ::= annotation_list annotation.

annotation ::= AT IDENTIFIER(N). {
    ctx->pending_annotations[N.as_string()] = "true";
}
annotation ::= AT IDENTIFIER(N) LPAREN STRING(V) RPAREN. {
    ctx->pending_annotations[N.as_string()] = strip_quotes(V.as_sv());
}
annotation ::= AT IDENTIFIER(N) LPAREN IDENTIFIER(V) RPAREN. {
    ctx->pending_annotations[N.as_string()] = V.as_string();
}

// --- Attributes ---
opt_attrs ::= .
opt_attrs ::= opt_attrs attribute.

attribute ::= SALIENCE INTEGER(V). {
    ctx->current_rule.salience = static_cast<int>(parse_int(V.as_sv()));
    ctx->current_rule.salience_explicitly_set = true;
}
attribute ::= EXTENDS STRING(V). {
    ctx->current_rule.parent_rule_name = strip_quotes(V.as_sv());
}
attribute ::= AGENDA_GROUP STRING(V). {
    ctx->current_rule.agenda_group = strip_quotes(V.as_sv());
}
attribute ::= ACTIVATION_GROUP STRING(V). {
    ctx->current_rule.activation_group = strip_quotes(V.as_sv());
}
attribute ::= TIMER INTEGER(V1). {
    ctx->current_rule.timer.emplace();
    ctx->current_rule.timer->initial_delay = parse_int(V1.as_sv());
}
attribute ::= TIMER INTEGER(V1) COMMA INTEGER(V2). {
    ctx->current_rule.timer.emplace();
    ctx->current_rule.timer->initial_delay = parse_int(V1.as_sv());
    ctx->current_rule.timer->repeat_interval = parse_int(V2.as_sv());
}
attribute ::= NO_LOOP. {
    ctx->current_rule.no_loop = true;
}
attribute ::= LOCK_ON_ACTIVE. {
    ctx->current_rule.lock_on_active = true;
}
attribute ::= ENABLED TRUE. {
    ctx->current_rule.enabled = true;
}
attribute ::= ENABLED FALSE. {
    ctx->current_rule.enabled = false;
}
attribute ::= AUTO_FOCUS TRUE. {
    ctx->current_rule.auto_focus = true;
}
attribute ::= AUTO_FOCUS FALSE. {
    ctx->current_rule.auto_focus = false;
}
attribute ::= DURATION_KW INTEGER(V). {
    ctx->current_rule.duration = parse_int(V.as_sv());
}

// --- When block ---
when_block ::= WHEN.
when_block ::= WHEN lhs.

// --- LHS ---
lhs ::= and_block(B). {
    ctx->current_condition_groups.push_back(std::move(*B));
    delete B;
}
lhs ::= lhs OR and_block(B). {
    ctx->current_condition_groups.push_back(std::move(*B));
    delete B;
}

// --- And block ---
and_block(A) ::= pattern(P). {
    A = new std::vector<ParsedPattern>();
    A->push_back(std::move(*P));
    delete P;
}
and_block(A) ::= and_block(B) pattern(P). {
    A = B;
    A->push_back(std::move(*P));
    delete P;
}
// Parenthesized and block
and_block(A) ::= LPAREN and_block(B) RPAREN. {
    A = B;
}

// --- RHS block ---
// RHS code chunk parsing - compiled as native RHS in semantic analysis
rhs_block ::= rhs_elements(E). {
    if (E) {
        ctx->current_rule.rhs_code = *E;
        normalize_indent(ctx->current_rule.rhs_code);
        delete E;
    }
}

rhs_elements(A) ::= . {
    A = new std::string();
}
rhs_elements(A) ::= rhs_elements(B) CODE_CHUNK(C). {
    A = B;
    A->append(C.as_sv());
    A->append("\n");
}
rhs_elements(A) ::= rhs_elements(B) modify_stmt(M). {
    A = B;
    A->append(*M);
    A->append("\n");
    delete M;
}

// --- Modify statement ---
%type modify_stmt { std::string* }
%destructor modify_stmt { delete $$; }

modify_stmt(A) ::= MODIFY LPAREN VARIABLE(T) RPAREN LBRACE modify_setters(S) RBRACE. {
    A = new std::string("rfl.update(" + T.as_string() + ", {" + *S + "})");
    delete S;
}

%type modify_setters { std::string* }
%destructor modify_setters { delete $$; }

modify_setters(A) ::= . {
    A = new std::string();
}
modify_setters(A) ::= modify_setters(B) modify_setter(S). {
    A = B;
    if (!A->empty() && !S->empty()) A->append(", ");
    A->append(*S);
    delete S;
}

%type modify_setter { std::string* }
%destructor modify_setter { delete $$; }

modify_setter(A) ::= IDENTIFIER(N) LPAREN setter_args(V) RPAREN opt_semi. {
    std::string setter_name(N.as_sv());
    std::string field_name = setter_name;
    if (setter_name.rfind("set", 0) == 0 && setter_name.length() > 3
        && std::isupper(setter_name[3])) {
        field_name = setter_name.substr(3);
        field_name[0] = static_cast<char>(std::tolower(field_name[0]));
    }
    A = new std::string(field_name + ": " + *V);
    delete V;
}

%type setter_args { std::string* }
%destructor setter_args { delete $$; }

setter_args(A) ::= . { A = new std::string(); }
setter_args(A) ::= setter_args_inner(B). { A = B; }

%type setter_args_inner { std::string* }
%destructor setter_args_inner { delete $$; }

// Capture balanced content inside setter parens
// setter_atom handles dotted paths; arithmetic operates on atoms
%type setter_atom { std::string* }
%destructor setter_atom { delete $$; }

setter_atom(A) ::= IDENTIFIER(T). { A = new std::string(T.as_string()); }
setter_atom(A) ::= VARIABLE(T). { A = new std::string(T.as_string()); }
setter_atom(A) ::= INTEGER(T). { A = new std::string(T.as_string()); }
setter_atom(A) ::= DOUBLE(T). { A = new std::string(T.as_string()); }
setter_atom(A) ::= STRING(T). { A = new std::string(T.as_string()); }
setter_atom(A) ::= TRUE. { A = new std::string("true"); }
setter_atom(A) ::= FALSE. { A = new std::string("false"); }
setter_atom(A) ::= NIL. { A = new std::string("nil"); }
setter_atom(A) ::= setter_atom(B) DOT IDENTIFIER(N). {
    A = B; A->append("."); A->append(N.as_sv());
}

setter_args_inner(A) ::= setter_atom(T). { A = T; }
setter_args_inner(A) ::= setter_args_inner(B) PLUS setter_atom(C). {
    A = B; A->append(" + "); A->append(*C); delete C;
}
setter_args_inner(A) ::= setter_args_inner(B) MINUS setter_atom(C). {
    A = B; A->append(" - "); A->append(*C); delete C;
}
setter_args_inner(A) ::= setter_args_inner(B) STAR setter_atom(C). {
    A = B; A->append(" * "); A->append(*C); delete C;
}
setter_args_inner(A) ::= setter_args_inner(B) SLASH setter_atom(C). {
    A = B; A->append(" / "); A->append(*C); delete C;
}
// --- Pattern with optional binding ---
pattern(A) ::= opt_binding(B) pattern_body(P). {
    A = P;
    if (B) { A->binding = *B; delete B; }
}

opt_binding(A) ::= . { A = nullptr; }
opt_binding(A) ::= VARIABLE(V) COLON. {
    A = new std::string(V.as_string());
}

// --- Pattern body ---
%type pattern_body { ParsedPattern* }
%destructor pattern_body { delete $$; }

pattern_body(A) ::= standard_pattern(P). { A = P; }
pattern_body(A) ::= not_pattern(P). { A = P; }
pattern_body(A) ::= exists_pattern(P). { A = P; }
pattern_body(A) ::= forall_pattern(P). { A = P; }
pattern_body(A) ::= eval_pattern(P). { A = P; }
pattern_body(A) ::= query_call_pattern(P). { A = P; }

// --- Standard pattern: FactType(constraints) opt_window opt_from ---
%type standard_pattern { ParsedPattern* }
%destructor standard_pattern { delete $$; }

standard_pattern(A) ::= qualified_name(N) opt_pattern_constraints(C) opt_window_decl(W) opt_from_clause(F). {
    A = new ParsedPattern();
    A->type = PatternType::STANDARD;
    A->fact_type = *N;
    if (C) { A->constraint_root.reset(C); }
    if (W) { A->window_info = *W; delete W; }
    if (F) { A->source = std::move(*F); delete F; }
    delete N;
}

// --- Optional Sliding Window ---
%type opt_window_decl { ParsedWindow* }
%destructor opt_window_decl { delete $$; }

opt_window_decl(A) ::= . { A = nullptr; }
opt_window_decl(A) ::= OVER WINDOW COLON TIME LPAREN DURATION(D) RPAREN. {
    A = new ParsedWindow{WindowType::TIME, parse_duration_ms(D.as_sv())};
}
opt_window_decl(A) ::= OVER WINDOW COLON TIME LPAREN INTEGER(I) RPAREN. {
    A = new ParsedWindow{WindowType::TIME, parse_int(I.as_sv())};
}
opt_window_decl(A) ::= OVER WINDOW COLON LENGTH LPAREN INTEGER(L) RPAREN. {
    A = new ParsedWindow{WindowType::LENGTH, parse_int(L.as_sv())};
}

// --- Optional pattern constraints ---
%type opt_pattern_constraints { ConstraintNode* }
%destructor opt_pattern_constraints { delete $$; }

opt_pattern_constraints(A) ::= . { A = nullptr; }
opt_pattern_constraints(A) ::= LPAREN RPAREN. { A = nullptr; }
opt_pattern_constraints(A) ::= LPAREN constraint_expr(E) RPAREN. { A = E; }

// --- Not pattern ---
%type not_pattern { ParsedPattern* }
%destructor not_pattern { delete $$; }

not_pattern(A) ::= NOT LPAREN pattern(P) RPAREN. {
    A = new ParsedPattern();
    A->type = PatternType::NOT;
    A->nested_patterns.push_back(std::move(*P));
    delete P;
}
not_pattern(A) ::= NOT standard_pattern(P). {
    A = new ParsedPattern();
    A->type = PatternType::NOT;
    A->nested_patterns.push_back(std::move(*P));
    delete P;
}

// --- Exists pattern ---
%type exists_pattern { ParsedPattern* }
%destructor exists_pattern { delete $$; }

exists_pattern(A) ::= EXISTS LPAREN pattern(P) RPAREN. {
    A = new ParsedPattern();
    A->type = PatternType::EXISTS;
    A->nested_patterns.push_back(std::move(*P));
    delete P;
}
exists_pattern(A) ::= EXISTS standard_pattern(P). {
    A = new ParsedPattern();
    A->type = PatternType::EXISTS;
    A->nested_patterns.push_back(std::move(*P));
    delete P;
}

// --- Forall pattern ---
%type forall_pattern { ParsedPattern* }
%destructor forall_pattern { delete $$; }

forall_pattern(A) ::= FORALL LPAREN pattern_list(L) RPAREN. {
    A = new ParsedPattern();
    A->type = PatternType::FORALL;
    A->forall_info.emplace();
    A->forall_info->patterns = std::move(*L);
    delete L;
}

pattern_list(A) ::= pattern(P). {
    A = new std::vector<ParsedPattern>();
    A->push_back(std::move(*P));
    delete P;
}
pattern_list(A) ::= pattern_list(B) pattern(P). {
    A = B;
    A->push_back(std::move(*P));
    delete P;
}
pattern_list(A) ::= pattern_list(B) COMMA pattern(P). {
    A = B;
    A->push_back(std::move(*P));
    delete P;
}

// --- Eval pattern ---
%type eval_pattern { ParsedPattern* }
%destructor eval_pattern { delete $$; }

eval_pattern(A) ::= EVAL LPAREN eval_content(E) RPAREN. {
    A = new ParsedPattern();
    A->type = PatternType::EVAL;
    A->eval_expression = *E;
    delete E;
}

%type eval_content { std::string* }
%destructor eval_content { delete $$; }

eval_content(A) ::= . { A = new std::string(); }
eval_content(A) ::= eval_content(B) IDENTIFIER(T). { A = B; if (!A->empty()) A->append(" "); A->append(T.as_sv()); }
eval_content(A) ::= eval_content(B) VARIABLE(T). { A = B; if (!A->empty()) A->append(" "); A->append(T.as_sv()); }
eval_content(A) ::= eval_content(B) INTEGER(T). { A = B; if (!A->empty()) A->append(" "); A->append(T.as_sv()); }
eval_content(A) ::= eval_content(B) DOUBLE(T). { A = B; if (!A->empty()) A->append(" "); A->append(T.as_sv()); }
eval_content(A) ::= eval_content(B) STRING(T). { A = B; if (!A->empty()) A->append(" "); A->append(T.as_sv()); }
eval_content(A) ::= eval_content(B) DOT. { A = B; A->append("."); }
eval_content(A) ::= eval_content(B) EQ. { A = B; A->append(" == "); }
eval_content(A) ::= eval_content(B) NE. { A = B; A->append(" != "); }
eval_content(A) ::= eval_content(B) LT. { A = B; A->append(" < "); }
eval_content(A) ::= eval_content(B) GT. { A = B; A->append(" > "); }
eval_content(A) ::= eval_content(B) LE. { A = B; A->append(" <= "); }
eval_content(A) ::= eval_content(B) GE. { A = B; A->append(" >= "); }
eval_content(A) ::= eval_content(B) PLUS. { A = B; A->append(" + "); }
eval_content(A) ::= eval_content(B) MINUS. { A = B; A->append(" - "); }
eval_content(A) ::= eval_content(B) STAR. { A = B; A->append(" * "); }
eval_content(A) ::= eval_content(B) SLASH. { A = B; A->append(" / "); }
eval_content(A) ::= eval_content(B) AND_AND. { A = B; A->append(" && "); }
eval_content(A) ::= eval_content(B) OR_OR. { A = B; A->append(" || "); }
eval_content(A) ::= eval_content(B) BANG. { A = B; A->append("!"); }
eval_content(A) ::= eval_content(B) COMMA. { A = B; A->append(", "); }
eval_content(A) ::= eval_content(B) LPAREN eval_content(C) RPAREN. {
    A = B; A->append("("); A->append(*C); A->append(")"); delete C;
}

// --- Query call pattern ---
%type query_call_pattern { ParsedPattern* }
%destructor query_call_pattern { delete $$; }

query_call_pattern(A) ::= STRING(N) LPAREN query_call_args(ARGS) RPAREN. {
    A = new ParsedPattern();
    A->type = PatternType::QUERY_CALL;
    ParsedQueryCall call;
    call.query_name = strip_quotes(N.as_sv());
    call.arguments = std::move(*ARGS);
    A->source = std::move(call);
    delete ARGS;
}

%type query_call_args { std::vector<std::string>* }
%destructor query_call_args { delete $$; }

query_call_args(A) ::= . { A = new std::vector<std::string>(); }
query_call_args(A) ::= query_call_args_inner(B). { A = B; }
query_call_args(A) ::= query_call_args_inner(B) SEMICOLON. { A = B; }

%type query_call_args_inner { std::vector<std::string>* }
%destructor query_call_args_inner { delete $$; }

query_call_args_inner(A) ::= VARIABLE(V). {
    A = new std::vector<std::string>();
    A->push_back(V.as_string());
}
query_call_args_inner(A) ::= query_call_args_inner(B) COMMA VARIABLE(V). {
    A = B;
    A->push_back(V.as_string());
}

// constraint_expr = or_expr
constraint_expr(A) ::= or_expr(E). { A = E; }

// or_expr = and_expr (|| and_expr)*
or_expr(A) ::= and_expr(E). { A = E; }
or_expr(A) ::= or_expr(L) OR_OR and_expr(R). {
    if (L->type == NodeType::OR) {
        A = L;
        A->children.push_back(std::unique_ptr<ConstraintNode>(R));
    } else {
        A = new ConstraintNode(NodeType::OR);
        A->children.push_back(std::unique_ptr<ConstraintNode>(L));
        A->children.push_back(std::unique_ptr<ConstraintNode>(R));
    }
}

// and_expr = constraint_item ((&&|,) constraint_item)*
and_expr(A) ::= constraint_item(E). { A = E; }
and_expr(A) ::= and_expr(L) AND_AND constraint_item(R). {
    if (L->type == NodeType::AND) {
        A = L;
        A->children.push_back(std::unique_ptr<ConstraintNode>(R));
    } else {
        A = new ConstraintNode(NodeType::AND);
        A->children.push_back(std::unique_ptr<ConstraintNode>(L));
        A->children.push_back(std::unique_ptr<ConstraintNode>(R));
    }
}
and_expr(A) ::= and_expr(L) COMMA constraint_item(R). {
    if (L->type == NodeType::AND) {
        A = L;
        A->children.push_back(std::unique_ptr<ConstraintNode>(R));
    } else {
        A = new ConstraintNode(NodeType::AND);
        A->children.push_back(std::unique_ptr<ConstraintNode>(L));
        A->children.push_back(std::unique_ptr<ConstraintNode>(R));
    }
}

// --- Constraint items ---

// Temporal window: within 60s of $e1
constraint_item(A) ::= WITHIN DURATION(D) OF VARIABLE(V). {
    A = new ConstraintNode(NodeType::LEAF);
    ParsedTemporalConstraint tc;
    tc.op = TemporalOp::Within;
    tc.lhs_field = "timestamp";
    tc.rhs_binding_and_field = {V.as_string(), "timestamp"};
    tc.window_ms = parse_duration_ms(D.as_sv());
    A->constraint.temporal_constraint = tc;
}

// Temporal sequence: field after/before/coincides/during $binding.field
constraint_item(A) ::= field_ref(F) temporal_op(OP) field_ref(R). {
    A = new ConstraintNode(NodeType::LEAF);
    ParsedTemporalConstraint tc;
    tc.op = *OP;
    tc.lhs_field = *F;
    std::string rhs = *R;
    size_t dot_pos = rhs.find('.');
    if (dot_pos != std::string::npos && rhs[0] == '$') {
        tc.rhs_binding_and_field = {rhs.substr(0, dot_pos), rhs.substr(dot_pos + 1)};
    }
    A->constraint.temporal_constraint = tc;
    delete F; delete R; delete OP;
}

%type temporal_op { TemporalOp* }
%destructor temporal_op { delete $$; }

temporal_op(A) ::= AFTER. { A = new TemporalOp(TemporalOp::After); }
temporal_op(A) ::= BEFORE. { A = new TemporalOp(TemporalOp::Before); }
temporal_op(A) ::= COINCIDES. { A = new TemporalOp(TemporalOp::Coincides); }
temporal_op(A) ::= DURING. { A = new TemporalOp(TemporalOp::During); }

// Exact non-negative integer bit-mask predicate.
constraint_item(A) ::= HAS_FLAG LPAREN field_ref(F) COMMA primary_expr_val(V) RPAREN. {
    A = new ConstraintNode(NodeType::LEAF);
    A->constraint.left_field = *F;
    A->constraint.op = CompareOp::HasFlag;
    set_rhs_value(A->constraint, *V);
    delete F; delete V;
}

// Relational with inline binding: $v : field cmp_op value
constraint_item(A) ::= VARIABLE(VB) COLON field_ref(F) cmp_op(OP) primary_expr_val(V). {
    A = new ConstraintNode(NodeType::LEAF);
    A->constraint.field_binding = VB.as_string();
    A->constraint.left_field = *F;
    A->constraint.op = *OP;
    set_rhs_value(A->constraint, *V);
    delete F; delete OP; delete V;
}
constraint_item(A) ::= VARIABLE(VB) COLON field_ref(F) cmp_op(OP) LPAREN arith_expr_str(E) RPAREN. {
    A = new ConstraintNode(NodeType::LEAF);
    A->constraint.field_binding = VB.as_string();
    A->constraint.left_field = *F;
    A->constraint.op = *OP;
    A->constraint.right_arith_expr = *E;
    delete F; delete OP; delete E;
}
constraint_item(A) ::= VARIABLE(VB) COLON field_ref(F) cmp_op(OP) IDENTIFIER(FN) LPAREN RPAREN. {
    A = new ConstraintNode(NodeType::LEAF);
    A->constraint.field_binding = VB.as_string();
    A->constraint.left_field = *F;
    A->constraint.op = *OP;
    A->constraint.right_arith_expr = FN.as_string() + "()";
    delete F; delete OP;
}

// Relational without inline binding
constraint_item(A) ::= field_ref(F) cmp_op(OP) primary_expr_val(V). {
    A = new ConstraintNode(NodeType::LEAF);
    A->constraint.left_field = *F;
    A->constraint.op = *OP;
    set_rhs_value(A->constraint, *V);
    delete F; delete OP; delete V;
}
constraint_item(A) ::= field_ref(F) cmp_op(OP) LPAREN arith_expr_str(E) RPAREN. {
    A = new ConstraintNode(NodeType::LEAF);
    A->constraint.left_field = *F;
    A->constraint.op = *OP;
    A->constraint.right_arith_expr = *E;
    delete F; delete OP; delete E;
}
constraint_item(A) ::= field_ref(F) cmp_op(OP) IDENTIFIER(FN) LPAREN RPAREN. {
    A = new ConstraintNode(NodeType::LEAF);
    A->constraint.left_field = *F;
    A->constraint.op = *OP;
    A->constraint.right_arith_expr = FN.as_string() + "()";
    delete F; delete OP;
}

// In clause: field in (values) / field not in (values)
constraint_item(A) ::= field_ref(F) IN LPAREN value_list(VL) RPAREN. {
    A = new ConstraintNode(NodeType::LEAF);
    A->constraint.left_field = *F;
    A->constraint.op = CompareOp::In;
    A->constraint.right_value_list = std::move(*VL);
    delete F; delete VL;
}
constraint_item(A) ::= field_ref(F) NOT IN LPAREN value_list(VL) RPAREN. {
    A = new ConstraintNode(NodeType::LEAF);
    A->constraint.left_field = *F;
    A->constraint.op = CompareOp::NotIn;
    A->constraint.right_value_list = std::move(*VL);
    delete F; delete VL;
}

// Bare field (boolean check): field  →  field == true
constraint_item(A) ::= field_ref(F). {
    A = new ConstraintNode(NodeType::LEAF);
    A->constraint.left_field = *F;
    A->constraint.op = CompareOp::EQ;
    A->constraint.right_literal = (int64_t)1;
    delete F;
}

// Inline binding only: $v : field (pure binding, no filter)
constraint_item(A) ::= VARIABLE(VB) COLON field_ref(F). {
    A = new ConstraintNode(NodeType::LEAF);
    A->constraint.field_binding = VB.as_string();
    A->constraint.left_field = *F;
    delete F;
}

// Inline binding with cmp: $v : field cmp_op value
// (already covered above)

// --- Comparison operators ---
%type cmp_op { CompareOp* }
%destructor cmp_op { delete $$; }

cmp_op(A) ::= EQ. { A = new CompareOp(CompareOp::EQ); }
cmp_op(A) ::= NE. { A = new CompareOp(CompareOp::NE); }
cmp_op(A) ::= GT. { A = new CompareOp(CompareOp::GT); }
cmp_op(A) ::= LT. { A = new CompareOp(CompareOp::LT); }
cmp_op(A) ::= GE. { A = new CompareOp(CompareOp::GE); }
cmp_op(A) ::= LE. { A = new CompareOp(CompareOp::LE); }
cmp_op(A) ::= CONTAINS. { A = new CompareOp(CompareOp::Contains); }
cmp_op(A) ::= NOT CONTAINS. { A = new CompareOp(CompareOp::NotContains); }
cmp_op(A) ::= CONTAINS_KEY. { A = new CompareOp(CompareOp::ContainsKey); }
cmp_op(A) ::= NOT CONTAINS_KEY. { A = new CompareOp(CompareOp::NotContainsKey); }
cmp_op(A) ::= MATCHES. { A = new CompareOp(CompareOp::Matches); }
cmp_op(A) ::= NOT MATCHES. { A = new CompareOp(CompareOp::NotMatches); }
cmp_op(A) ::= STARTS_WITH. { A = new CompareOp(CompareOp::StartsWith); }
cmp_op(A) ::= ENDS_WITH. { A = new CompareOp(CompareOp::EndsWith); }
cmp_op(A) ::= LENGTH_IS. { A = new CompareOp(CompareOp::LengthIs); }
cmp_op(A) ::= MEMBER_OF. { A = new CompareOp(CompareOp::MemberOf); }
cmp_op(A) ::= NOT MEMBER_OF. { A = new CompareOp(CompareOp::NotMemberOf); }

// --- Field reference (dotted path with optional $var prefix, null-safe, index) ---
%type field_ref { std::string* }
%destructor field_ref { delete $$; }

field_ref(A) ::= IDENTIFIER(N). { A = new std::string(N.as_string()); }
field_ref(A) ::= VARIABLE(V). { A = new std::string(V.as_string()); }
field_ref(A) ::= THIS. { A = new std::string("this"); }
field_ref(A) ::= field_ref(B) DOT IDENTIFIER(N). {
    A = B; A->append("."); A->append(N.as_sv());
}
field_ref(A) ::= field_ref(B) BANG_DOT IDENTIFIER(N). {
    A = B; A->append("!."); A->append(N.as_sv());
}
field_ref(A) ::= field_ref(B) LBRACKET INTEGER(I) RBRACKET. {
    A = B; A->append("["); A->append(I.as_sv()); A->append("]");
}
field_ref(A) ::= field_ref(B) LBRACKET STRING(S) RBRACKET. {
    A = B; A->append("["); A->append(S.as_sv()); A->append("]");
}

// --- Primary expression value ---
primary_expr_val(A) ::= INTEGER(T). { A = new ConstraintValue(parse_int(T.as_sv())); }
primary_expr_val(A) ::= DOUBLE(T). { A = new ConstraintValue(parse_dbl(T.as_sv())); }
primary_expr_val(A) ::= STRING(T). { A = new ConstraintValue(strip_quotes(T.as_sv())); }
primary_expr_val(A) ::= TRUE. { A = new ConstraintValue(true); }
primary_expr_val(A) ::= FALSE. { A = new ConstraintValue(false); }
primary_expr_val(A) ::= NIL. { A = new ConstraintValue(NilValue{}); }
primary_expr_val(A) ::= field_ref(F). {
    // Could be a bound field reference ($var.field) or a plain field name
    std::string const& s = *F;
    if (!s.empty() && s[0] == '$') {
        size_t dot_pos = s.find('.');
        if (dot_pos != std::string::npos) {
            // It's a bound field reference — store as string for now,
            // the caller (set_rhs_value) will parse it
            A = new ConstraintValue(s);
        } else {
            // Bare variable like $p → refers to whole fact
            A = new ConstraintValue(s);
        }
    } else {
        A = new ConstraintValue(s);
    }
    delete F;
}
// --- Arithmetic expression (as string for arith_expr support) ---
%type arith_expr_str { std::string* }
%destructor arith_expr_str { delete $$; }

arith_expr_str(A) ::= field_ref(F). { A = F; }
arith_expr_str(A) ::= INTEGER(T). { A = new std::string(T.as_string()); }
arith_expr_str(A) ::= DOUBLE(T). { A = new std::string(T.as_string()); }
arith_expr_str(A) ::= LPAREN arith_expr_str(E) RPAREN. {
    A = new std::string("(");
    A->append(*E);
    A->append(")");
    delete E;
}
arith_expr_str(A) ::= IDENTIFIER(FN) LPAREN RPAREN. {
    A = new std::string(FN.as_string());
    A->append("()");
}
arith_expr_str(A) ::= IDENTIFIER(FN) LPAREN arith_expr_str(E) RPAREN. {
    A = new std::string(FN.as_string());
    A->append("(");
    A->append(*E);
    A->append(")");
    delete E;
}
arith_expr_str(A) ::= IDENTIFIER(FN) LPAREN arith_expr_str(L) COMMA arith_expr_str(R) RPAREN. {
    A = new std::string(FN.as_string());
    A->append("(");
    A->append(*L);
    A->append(", ");
    A->append(*R);
    A->append(")");
    delete L;
    delete R;
}
arith_expr_str(A) ::= arith_expr_str(L) PLUS arith_expr_str(R). {
    A = L; A->append(" + "); A->append(*R); delete R;
}
arith_expr_str(A) ::= arith_expr_str(L) MINUS arith_expr_str(R). {
    A = L; A->append(" - "); A->append(*R); delete R;
}
arith_expr_str(A) ::= arith_expr_str(L) STAR arith_expr_str(R). {
    A = L; A->append(" * "); A->append(*R); delete R;
}
arith_expr_str(A) ::= arith_expr_str(L) SLASH arith_expr_str(R). {
    A = L; A->append(" / "); A->append(*R); delete R;
}

// --- Value list for IN clause ---
value_list(A) ::= primary_expr_val(V). {
    A = new std::vector<ConstraintValue>();
    A->push_back(std::move(*V));
    delete V;
}
value_list(A) ::= value_list(B) COMMA primary_expr_val(V). {
    A = B;
    A->push_back(std::move(*V));
    delete V;
}

// --- Optional from clause ---
%type opt_from_clause { PatternSource* }
%destructor opt_from_clause { delete $$; }

opt_from_clause(A) ::= . { A = nullptr; }
opt_from_clause(A) ::= from_clause(F). { A = F; }

%type from_clause { PatternSource* }
%destructor from_clause { delete $$; }

// from accumulate(SourcePattern(...), func(field))
from_clause(A) ::= FROM ACCUMULATE LPAREN accumulate_src_pat(SP) COMMA
                    IDENTIFIER(FN) LPAREN opt_accum_ref(FR) RPAREN RPAREN. {
    auto* acc = new ParsedAccumulate();
    acc->source_pattern = std::unique_ptr<ParsedPattern>(SP);
    acc->function = FN.as_string();
    if (FR) { acc->field = *FR; delete FR; }
    A = new PatternSource(std::move(*acc));
    delete acc;
}

// from collect(pattern)
from_clause(A) ::= FROM COLLECT LPAREN pattern(P) RPAREN. {
    auto* acc = new ParsedAccumulate();
    acc->function = "collect";
    if (!P->binding.empty()) { acc->field = P->binding; }
    acc->source_pattern = std::unique_ptr<ParsedPattern>(P);
    A = new PatternSource(std::move(*acc));
    delete acc;
}

// from unnest($binding.field)
from_clause(A) ::= FROM UNNEST LPAREN field_ref(F) RPAREN. {
    ParsedUnnest u;
    std::string const& src = *F;
    size_t dot_pos = src.find('.');
    if (dot_pos != std::string::npos && src[0] == '$') {
        u.source_binding = src.substr(0, dot_pos);
        u.source_field = src.substr(dot_pos + 1);
    }
    A = new PatternSource(std::move(u));
    delete F;
}


// from entry-point "name"
from_clause(A) ::= FROM ENTRY_POINT STRING(N). {
    A = new PatternSource(strip_quotes(N.as_sv()));
}

// --- Accumulate source pattern ---
accumulate_src_pat(A) ::= opt_binding(B) qualified_name(N) opt_pattern_constraints(C) opt_window_decl(W) opt_accum_source_clause(S). {
    A = new ParsedPattern();
    A->type = PatternType::STANDARD;
    A->fact_type = *N;
    if (B) { A->binding = *B; delete B; }
    if (C) { A->constraint_root.reset(C); }
    if (W) { A->window_info = *W; delete W; }
    if (S) { A->source = std::move(*S); delete S; }
    delete N;
}

%type opt_accum_source_clause { PatternSource* }
%destructor opt_accum_source_clause { delete $$; }

opt_accum_source_clause(A) ::= . { A = nullptr; }
opt_accum_source_clause(A) ::= accum_source_clause(F). { A = F; }

%type accum_source_clause { PatternSource* }
%destructor accum_source_clause { delete $$; }

// Allowed source clauses for accumulate input pattern.
accum_source_clause(A) ::= FROM ENTRY_POINT STRING(N). {
    A = new PatternSource(strip_quotes(N.as_sv()));
}

// --- Optional accumulate field reference ---
%type opt_accum_ref { std::string* }
%destructor opt_accum_ref { delete $$; }

opt_accum_ref(A) ::= . { A = nullptr; }
opt_accum_ref(A) ::= accum_ref(R). { A = R; }

%type accum_ref { std::string* }
%destructor accum_ref { delete $$; }

// Simple field, variable, integer, or arithmetic expression
accum_ref(A) ::= field_ref(F). { A = F; }
accum_ref(A) ::= INTEGER(T). { A = new std::string(T.as_string()); }
accum_ref(A) ::= DOUBLE(T). { A = new std::string(T.as_string()); }
accum_ref(A) ::= IDENTIFIER(FN) LPAREN RPAREN. {
    A = new std::string(FN.as_string());
    A->append("()");
}
accum_ref(A) ::= IDENTIFIER(FN) LPAREN accum_ref(E) RPAREN. {
    A = new std::string(FN.as_string());
    A->append("(");
    A->append(*E);
    A->append(")");
    delete E;
}
accum_ref(A) ::= IDENTIFIER(FN) LPAREN accum_ref(L) COMMA accum_ref(R) RPAREN. {
    A = new std::string(FN.as_string());
    A->append("(");
    A->append(*L);
    A->append(", ");
    A->append(*R);
    A->append(")");
    delete L; delete R;
}
accum_ref(A) ::= accum_ref(L) PLUS accum_ref(R). {
    A = L; A->append(" + "); A->append(*R); delete R;
}
accum_ref(A) ::= accum_ref(L) MINUS accum_ref(R). {
    A = L; A->append(" - "); A->append(*R); delete R;
}
accum_ref(A) ::= accum_ref(L) STAR accum_ref(R). {
    A = L; A->append(" * "); A->append(*R); delete R;
}
accum_ref(A) ::= accum_ref(L) SLASH accum_ref(R). {
    A = L; A->append(" / "); A->append(*R); delete R;
}

// ===================================================================
// Query statement
// ===================================================================

query_stmt ::= QUERY(Q) query_name(N) opt_query_params query_lhs END. {
    ctx->current_query.name = *N;
    ctx->current_query.pos = {ctx->source_name, Q.line, Q.column};
    ctx->current_query.source_package = ctx->state.package_name;
    ctx->current_query.source_imports = ctx->state.parsed_imports;
    ctx->state.parsed_queries.push_back(std::move(ctx->current_query));
    ctx->current_query = ParsedQuery{};
    delete N;
}

%type query_name { std::string* }
%destructor query_name { delete $$; }

query_name(A) ::= STRING(T). { A = new std::string(strip_quotes(T.as_sv())); }
query_name(A) ::= IDENTIFIER(T). { A = new std::string(T.as_string()); }

// --- Query parameters ---
opt_query_params ::= .
opt_query_params ::= LPAREN query_param_list RPAREN.

query_param_list ::= .
query_param_list ::= query_param.
query_param_list ::= query_param_list COMMA query_param.

query_param ::= qualified_name(T) VARIABLE(V). {
    ParsedPattern p;
    p.type = PatternType::STANDARD;
    p.fact_type = *T;
    p.binding = V.as_string();
    ctx->current_query.parameter_types.push_back(*T);
    ctx->current_query.parameter_count++;
    ctx->current_query.patterns.push_back(std::move(p));
    delete T;
}

// --- Query LHS (optional patterns before END) ---
query_lhs ::= .
query_lhs ::= query_lhs_patterns.

query_lhs_patterns ::= pattern(P). {
    ctx->current_query.patterns.push_back(std::move(*P));
    delete P;
}
query_lhs_patterns ::= query_lhs_patterns pattern(P). {
    ctx->current_query.patterns.push_back(std::move(*P));
    delete P;
}
