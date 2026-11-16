#include "expression_descriptor.hpp"
#include "scalar_lexer.hpp"
#include "execution/scalar_function.hpp"
#include "text/scalar_text.hpp"
#include <mir.h>
#include <mir-gen.h>
#include <array>
#include <bit>
#include <cstddef>
#include <exception>
#include <limits>
#include <span>
#include <stdexcept>
#include <utility>

namespace rulesforge {
namespace {
using scalar::Lexeme;
using scalar::Token;
using Descriptor = ExpressionDescriptor;
enum class Operation { Literal, Variable, Add, Sub, Mul, Div, Neg, Plus, Not, And, Or, Compare, Function };
struct Node {
    Operation operation;
    size_t left = 0, right = 0, argument = 0, depth = 1;
    ConstraintValue literal = NilValue{};
};
struct Syntax {
    std::vector<Node> nodes;
    std::vector<Descriptor::Variable> variables;
    std::vector<std::string> names;
    size_t root = 0;
};

std::string decoded_string(std::string_view token) {
    auto raw = token.substr(1, token.size() - 2);
    OwnedText owner;
    size_t begin = 0;
    for (size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\') continue;
        owner.append(raw.substr(begin, i - begin));
        char value = raw[++i]; // Lexer admits a character after each backslash.
        switch (value) {
            case 'n': value = '\n'; break;
            case 'r': value = '\r'; break;
            case 't': value = '\t'; break;
            case '\\': case '\'': case '"': break;
            default: throw std::invalid_argument("Unsupported scalar string escape");
        }
        owner.append(std::string_view(&value, 1));
        begin = i + 1;
    }
    owner.append(raw.substr(begin));
    return std::string(owner.view());
}

class Parser {
    scalar::Lexer lexer_;
    Token token_;
    Syntax syntax_;
    size_t nesting_ = 0;
    void next() { token_ = lexer_.next(); }
    void expect(Lexeme kind) {
        if (token_.kind != kind) throw std::invalid_argument("Unexpected scalar expression token");
        next();
    }
    size_t add(Node node) {
        if (syntax_.nodes.size() == Descriptor::max_operations || node.depth > Descriptor::max_depth)
            throw std::length_error("Scalar expression operation/depth limit exceeded");
        syntax_.nodes.push_back(std::move(node));
        return syntax_.nodes.size() - 1;
    }
    size_t literal(ConstraintValue value) {
        Node node{Operation::Literal};
        node.literal = std::move(value);
        return add(std::move(node));
    }
    static int precedence(Lexeme kind) {
        switch (kind) {
            case Lexeme::Or: return 1;
            case Lexeme::And: return 2;
            case Lexeme::EQ: case Lexeme::NE: case Lexeme::GT:
            case Lexeme::GE: case Lexeme::LT: case Lexeme::LE: return 3;
            case Lexeme::Plus: case Lexeme::Minus: return 4;
            case Lexeme::Star: case Lexeme::Slash: return 5;
            default: return 0;
        }
    }
    static Operation operation(Lexeme kind) {
        switch (kind) {
            case Lexeme::Plus: return Operation::Add;
            case Lexeme::Minus: return Operation::Sub;
            case Lexeme::Star: return Operation::Mul;
            case Lexeme::Slash: return Operation::Div;
            case Lexeme::And: return Operation::And;
            case Lexeme::Or: return Operation::Or;
            default: return Operation::Compare;
        }
    }
    size_t variable() {
        Descriptor::Variable access;
        access.binding = token_.text;
        access.name = access.binding;
        next();
        while (token_.kind == Lexeme::Dot || token_.kind == Lexeme::LeftIndex) {
            if (token_.kind == Lexeme::Dot) {
                next();
                if (token_.kind != Lexeme::Identifier)
                    throw std::invalid_argument("Expected scalar field name");
                access.name.append(".").append(token_.text);
                next();
            } else {
                next();
                if (token_.kind != Lexeme::Number && token_.kind != Lexeme::String)
                    throw std::invalid_argument("Expected scalar field index");
                access.name.append("[").append(token_.text).append("]");
                next();
                expect(Lexeme::RightIndex);
            }
        }
        auto dot = access.name.find('.');
        if (dot == std::string::npos && access.name.find('[') != std::string::npos)
            throw std::invalid_argument("Indexing requires an explicit scalar field");
        access.field = dot == std::string::npos ? "this" : access.name.substr(dot + 1);
        access.path = parse_field_path(access.field);
        access.unqualified_path = parse_field_path(access.binding.substr(1));
        size_t ordinal = 0;
        for (; ordinal < syntax_.names.size(); ++ordinal)
            if (syntax_.names[ordinal] == access.name) break;
        if (ordinal == syntax_.names.size()) {
            if (ordinal == Descriptor::max_variables)
                throw std::length_error("Scalar expression variable limit exceeded");
            syntax_.names.push_back(access.name);
            syntax_.variables.push_back(std::move(access));
        }
        Node node{Operation::Variable};
        node.argument = ordinal;
        return add(std::move(node));
    }
    size_t primary() {
        if (token_.kind == Lexeme::Open) {
            next();
            auto index = expression(1);
            expect(Lexeme::Close);
            return index;
        }
        if (token_.kind == Lexeme::Variable) return variable();
        if (token_.kind == Lexeme::Number) {
            auto number = parse_scalar_number(token_.text);
            if (!number) throw std::invalid_argument("Invalid scalar number");
            next();
            return literal(std::move(*number));
        }
        if (token_.kind == Lexeme::String) {
            auto value = decoded_string(token_.text);
            next();
            return literal(std::move(value));
        }
        if (token_.kind != Lexeme::Identifier)
            throw std::invalid_argument("Expected scalar expression operand");
        auto name = token_.text;
        next();
        if (name == "true") return literal(true);
        if (name == "false") return literal(false);
        if (name == "nil" || name == "null") return literal(NilValue{});
        if (name == "pi") return literal(3.14159265358979323846);
        auto function = parse_scalar_function(name);
        expect(Lexeme::Open);
        std::array<size_t, 2> args{};
        size_t count = 0;
        if (token_.kind != Lexeme::Close) {
            for (;;) {
                if (count == args.size())
                    throw std::invalid_argument("Scalar function argument count mismatch");
                args[count++] = expression(1);
                if (token_.kind != Lexeme::Comma) break;
                next();
            }
        }
        expect(Lexeme::Close);
        if (count != scalar_function_arity(function))
            throw std::invalid_argument("Scalar function argument count mismatch");
        Node node{Operation::Function, args[0], count == 2 ? args[1] : args[0]};
        node.argument = static_cast<size_t>(function);
        node.depth = 1 + std::max(syntax_.nodes[node.left].depth, syntax_.nodes[node.right].depth);
        return add(std::move(node));
    }
    size_t unary() {
        if (++nesting_ > Descriptor::max_depth)
            throw std::length_error("Scalar expression nesting limit exceeded");
        struct Depth { size_t& value; ~Depth() { --value; } } depth{nesting_};
        auto kind = token_.kind;
        if (kind != Lexeme::Plus && kind != Lexeme::Minus && kind != Lexeme::Not)
            return primary();
        next();
        // INT64_MIN must not first become an overflowing positive integer.
        if (kind == Lexeme::Minus && token_.kind == Lexeme::Number) {
            auto value = parse_scalar_number("-" + std::string(token_.text));
            if (!value) throw std::invalid_argument("Invalid negative scalar number");
            next();
            return literal(std::move(*value));
        }
        size_t child = unary();
        Node node{kind == Lexeme::Minus ? Operation::Neg :
                  kind == Lexeme::Plus ? Operation::Plus : Operation::Not, child};
        node.depth = syntax_.nodes[child].depth + 1;
        return add(std::move(node));
    }
    size_t expression(int minimum) {
        size_t left = unary();
        while (precedence(token_.kind) >= minimum) {
            auto kind = token_.kind;
            auto rank = precedence(kind);
            next();
            size_t right = expression(rank + 1);
            Node node{operation(kind), left, right};
            node.argument = static_cast<size_t>(kind);
            node.depth = 1 + std::max(syntax_.nodes[left].depth, syntax_.nodes[right].depth);
            left = add(std::move(node));
        }
        return left;
    }
public:
    explicit Parser(std::string const& source) : lexer_(source) { next(); }
    Syntax parse() {
        syntax_.root = expression(1);
        if (token_.kind != Lexeme::End)
            throw std::invalid_argument("Trailing scalar expression input");
        return std::move(syntax_);
    }
};

enum Kind : int64_t { Integer, Unsigned, Number, Boolean, Other };
enum Status : int64_t { Ok, Missing, HostRequest, WrongType, DivideByZero, Overflow, NonFinite };
struct Slot {
    int64_t kind = Other;
    int64_t integer = 0;
    double number = 0;
    ConstraintValue const* original = nullptr;
};
// Only this standard-layout prefix is visible to generated code.
struct Frame {
    std::array<Slot, Descriptor::max_operations> slots{};
    void* state;
    int64_t continuation = 0;
    int64_t request = 0;
};
static_assert(std::is_standard_layout_v<Slot> && std::is_standard_layout_v<Frame>);
struct State {
    std::vector<Descriptor::Variable> const& variables;
    std::array<std::optional<ConstraintValue>, Descriptor::max_variables> values{};
    std::array<Slot, Descriptor::max_variables> cached{};
};
void pack_numeric(Slot& slot, int64_t value) {
    slot.kind = Integer; slot.integer = value; slot.number = static_cast<double>(value);
}
void pack_numeric(Slot& slot, uint64_t value) {
    slot.kind = Unsigned; slot.integer = std::bit_cast<int64_t>(value);
    slot.number = static_cast<double>(value);
}
void pack_numeric(Slot& slot, double value) {
    if (!std::isfinite(value)) throw std::domain_error("Non-finite scalar input");
    slot.kind = Number; slot.number = value;
}
Slot pack(ConstraintValue const& value) {
    Slot slot;
    slot.original = &value;
    if (auto v = std::get_if<int64_t>(&value)) pack_numeric(slot, *v);
    else if (auto v = std::get_if<uint64_t>(&value)) pack_numeric(slot, *v);
    else if (auto v = std::get_if<double>(&value)) pack_numeric(slot, *v);
    else if (auto v = std::get_if<DurationValue>(&value)) pack_numeric(slot, v->milliseconds);
    else if (auto v = std::get_if<EnumValue>(&value))
        std::visit([&](auto number) { pack_numeric(slot, number); }, v->value);
    else if (auto v = std::get_if<bool>(&value)) { slot.kind = Boolean; slot.integer = *v; }
    return slot;
}
ConstraintValue unpack(Slot const& slot) {
    if (slot.original) return *slot.original;
    switch (slot.kind) {
        case Integer: return slot.integer;
        case Unsigned: return std::bit_cast<uint64_t>(slot.integer);
        case Number: return slot.number;
        case Boolean: return slot.integer != 0;
        default: throw std::logic_error("Invalid MIR scalar output");
    }
}
// The only variable helper callable from native code reads an already packed
// value. All allocations, resolver callbacks and exceptions stay in C++ frames.
int64_t load_cached(Frame* frame, int64_t output, int64_t ordinal) noexcept {
    auto& state = *static_cast<State*>(frame->state);
    if (!state.values[ordinal]) return Missing;
    frame->slots[output] = state.cached[ordinal];
    return Ok;
}
int64_t truth(Frame* frame, int64_t index) noexcept {
    auto const& slot = frame->slots[index];
    if (slot.kind == Integer || slot.kind == Unsigned || slot.kind == Boolean)
        return slot.integer != 0;
    if (slot.kind == Number) return std::fpclassify(slot.number) != FP_ZERO;
    return Descriptor::truthy(*slot.original);
}
bool ordered(Lexeme operation, int order) {
    switch (operation) {
        case Lexeme::EQ: return order == 0;
        case Lexeme::NE: return order != 0;
        case Lexeme::GT: return order > 0;
        case Lexeme::GE: return order >= 0;
        case Lexeme::LT: return order < 0;
        case Lexeme::LE: return order <= 0;
        default: throw std::logic_error("Invalid MIR comparison operation");
    }
}
int numeric_order(Slot const& left, Slot const& right) {
    auto integer_double = [](Slot const& integer, double number) {
        if (integer.kind == Integer) {
            constexpr double lower = -9223372036854775808.0, upper = 9223372036854775808.0;
            if (number < lower) return 1;
            if (number >= upper) return -1;
            auto truncated = static_cast<int64_t>(number);
            if (integer.integer != truncated) return integer.integer < truncated ? -1 : 1;
            return number > static_cast<double>(truncated) ? -1 :
                   number < static_cast<double>(truncated) ? 1 : 0;
        }
        constexpr double upper = 18446744073709551616.0;
        if (number < 0) return 1;
        if (number >= upper) return -1;
        auto truncated = static_cast<uint64_t>(number);
        auto value = std::bit_cast<uint64_t>(integer.integer);
        if (value != truncated) return value < truncated ? -1 : 1;
        return number > static_cast<double>(truncated) ? -1 : 0;
    };
    if (left.kind == Number && right.kind != Number) return -integer_double(right, left.number);
    if (right.kind == Number && left.kind != Number) return integer_double(left, right.number);
    if (left.kind == Number)
        return left.number < right.number ? -1 : left.number > right.number ? 1 : 0;
    if (left.kind == Integer && right.kind == Integer)
        return left.integer < right.integer ? -1 : left.integer > right.integer ? 1 : 0;
    if (left.kind == Integer && left.integer < 0) return -1;
    if (right.kind == Integer && right.integer < 0) return 1;
    auto a = std::bit_cast<uint64_t>(left.integer), b = std::bit_cast<uint64_t>(right.integer);
    return a < b ? -1 : a > b ? 1 : 0;
}
int64_t compare(Frame* frame, int64_t output, int64_t operation, int64_t a, int64_t b) {
    auto const& left = frame->slots[a];
    auto const& right = frame->slots[b];
    auto op = static_cast<Lexeme>(operation);
    bool result;
    if (left.kind <= Number && right.kind <= Number) {
        result = ordered(op, numeric_order(left, right));
    } else {
        std::optional<ConstraintValue> left_value, right_value;
        if (!left.original) left_value = unpack(left);
        if (!right.original) right_value = unpack(right);
        auto const& lv = left.original ? *left.original : *left_value;
        auto const& rv = right.original ? *right.original : *right_value;
        ScalarTextView lt(lv), rt(rv);
        if (lt && rt) result = ordered(op, lt->compare(*rt));
        else if (op == Lexeme::EQ || op == Lexeme::NE) {
            bool equal = ConstraintValueEquals{}(lv, rv);
            result = op == Lexeme::EQ ? equal : !equal;
        } else result = false;
    }
    frame->slots[output] = Slot{Boolean, result};
    return Ok;
}
int64_t function(Frame* frame, int64_t output, int64_t operation, int64_t a, int64_t b) {
    auto op = static_cast<ScalarFunction>(operation);
    auto const& left = frame->slots[a];
    auto const& right = frame->slots[b];
    if (left.kind > Number || (scalar_function_arity(op) == 2 && right.kind > Number))
        return WrongType;
    std::array<double, 2> args{left.number, right.number};
    frame->slots[output] = Slot{Number, 0,
        apply_scalar_function(op, std::span<double const>(args.data(), scalar_function_arity(op)))};
    return Ok;
}
int64_t finite(double value) noexcept { return std::isfinite(value); }

struct MirOwner {
    MIR_context_t context = MIR_init();
    bool generator = false;
    MirOwner() { if (!context) throw std::bad_alloc(); }
    ~MirOwner() {
        if (generator) MIR_gen_finish(context);
        MIR_finish(context);
    }
    MirOwner(MirOwner const&) = delete;
    MirOwner& operator=(MirOwner const&) = delete;
};
class Generator {
    MIR_context_t ctx_;
    MIR_item_t function_;
    MIR_reg_t frame_, status_, flag_, integer_, number_;
    MIR_label_t wrong_type_, divide_, overflow_, nonfinite_;
    struct Helper { MIR_item_t prototype, symbol; };
    Helper load_, truth_, finite_;
    std::vector<MIR_label_t> continuations_;
    Syntax const& syntax_;
    MIR_op_t reg(MIR_reg_t value) { return MIR_new_reg_op(ctx_, value); }
    MIR_op_t imm(int64_t value) { return MIR_new_int_op(ctx_, value); }
    MIR_op_t real(double value) { return MIR_new_double_op(ctx_, value); }
    MIR_op_t ref(MIR_item_t value) { return MIR_new_ref_op(ctx_, value); }
    MIR_op_t label(MIR_label_t value) { return MIR_new_label_op(ctx_, value); }
    MIR_op_t memory(size_t slot, size_t member, MIR_type_t type) {
        auto offset = offsetof(Frame, slots) + slot * sizeof(Slot) + member;
        return MIR_new_mem_op(ctx_, type, offset, frame_, 0, 1);
    }
    MIR_op_t kind(size_t slot) { return memory(slot, offsetof(Slot, kind), MIR_T_I64); }
    MIR_op_t integer(size_t slot) { return memory(slot, offsetof(Slot, integer), MIR_T_I64); }
    MIR_op_t number(size_t slot) { return memory(slot, offsetof(Slot, number), MIR_T_D); }
    MIR_op_t original(size_t slot) { return memory(slot, offsetof(Slot, original), MIR_T_P); }
    MIR_op_t continuation() {
        return MIR_new_mem_op(ctx_, MIR_T_I64, offsetof(Frame, continuation), frame_, 0, 1);
    }
    MIR_op_t request() {
        return MIR_new_mem_op(ctx_, MIR_T_I64, offsetof(Frame, request), frame_, 0, 1);
    }
    template<class... Operands>
    void emit(MIR_insn_code_t code, Operands... operands) {
        std::array<MIR_op_t, sizeof...(operands)> args{operands...};
        MIR_append_insn(ctx_, function_, MIR_new_insn_arr(ctx_, code, args.size(), args.data()));
    }
    void mark(MIR_label_t point) { MIR_append_insn(ctx_, function_, point); }
    void jump(MIR_label_t point) { emit(MIR_JMP, label(point)); }
    Helper helper(char const* name, void* address, size_t integer_args, bool double_arg = false) {
        auto result = MIR_T_I64;
        std::array<MIR_var_t, 5> args{};
        static char const* names[] = {"frame", "output", "op", "left", "right"};
        size_t count = double_arg ? 1 : integer_args + 1;
        for (size_t i = 0; i < count; ++i) {
            args[i].type = double_arg ? MIR_T_D : i == 0 ? MIR_T_P : MIR_T_I64;
            args[i].name = names[i];
        }
        auto prototype_name = std::string(name) + "_prototype";
        auto prototype = MIR_new_proto_arr(ctx_, prototype_name.c_str(), 1, &result, count, args.data());
        auto symbol = MIR_new_import(ctx_, name);
        MIR_load_external(ctx_, name, address);
        return {prototype, symbol};
    }
    template<class... Operands>
    void call(Helper helper, MIR_reg_t output, Operands... operands) {
        emit(MIR_CALL, ref(helper.prototype), ref(helper.symbol), reg(output), operands...);
    }
    void host_request(size_t index) {
        auto resume = MIR_new_label(ctx_);
        emit(MIR_MOV, continuation(), imm(continuations_.size()));
        emit(MIR_MOV, request(), imm(index));
        emit(MIR_RET, imm(HostRequest));
        continuations_.push_back(resume);
        mark(resume);
    }
    void numeric_check(size_t index) { emit(MIR_BGT, label(wrong_type_), kind(index), imm(Number)); }
    void finite_check() {
        call(finite_, flag_, reg(number_));
        emit(MIR_BF, label(nonfinite_), reg(flag_));
    }
    void store_number(size_t output) {
        emit(MIR_MOV, kind(output), imm(Number));
        emit(MIR_DMOV, number(output), reg(number_));
        emit(MIR_MOV, original(output), imm(0));
    }
    void store_integer(size_t output) {
        emit(MIR_MOV, kind(output), imm(Integer));
        emit(MIR_MOV, integer(output), reg(integer_));
        emit(MIR_I2D, reg(number_), reg(integer_));
        emit(MIR_DMOV, number(output), reg(number_));
        emit(MIR_MOV, original(output), imm(0));
    }
    void emit_node(size_t index) {
        auto const& node = syntax_.nodes[index];
        if (node.operation == Operation::Literal) {
            auto slot = pack(node.literal);
            emit(MIR_MOV, kind(index), imm(slot.kind));
            emit(MIR_MOV, integer(index), imm(slot.integer));
            emit(MIR_DMOV, number(index), real(slot.number));
            emit(MIR_MOV, original(index), MIR_new_uint_op(ctx_, reinterpret_cast<uintptr_t>(&node.literal)));
            return;
        }
        if (node.operation == Operation::Variable) {
            auto ready = MIR_new_label(ctx_);
            call(load_, status_, reg(frame_), imm(index), imm(node.argument));
            emit(MIR_BF, label(ready), reg(status_));
            host_request(index);
            mark(ready);
            return;
        }
        emit_node(node.left);
        if (node.operation == Operation::And || node.operation == Operation::Or) {
            auto done = MIR_new_label(ctx_);
            call(truth_, flag_, reg(frame_), imm(node.left));
            emit(MIR_MOV, kind(index), imm(Boolean));
            emit(MIR_MOV, original(index), imm(0));
            emit(MIR_MOV, integer(index), reg(flag_));
            emit(node.operation == Operation::And ? MIR_BF : MIR_BT, label(done), reg(flag_));
            emit_node(node.right);
            call(truth_, flag_, reg(frame_), imm(node.right));
            emit(MIR_MOV, integer(index), reg(flag_));
            mark(done);
            return;
        }
        if (node.operation == Operation::Not) {
            call(truth_, flag_, reg(frame_), imm(node.left));
            emit(MIR_EQ, reg(flag_), reg(flag_), imm(0));
            emit(MIR_MOV, kind(index), imm(Boolean));
            emit(MIR_MOV, integer(index), reg(flag_));
            emit(MIR_MOV, original(index), imm(0));
            return;
        }
        if (node.operation == Operation::Neg || node.operation == Operation::Plus) {
            numeric_check(node.left);
            auto floating = MIR_new_label(ctx_), done = MIR_new_label(ctx_);
            emit(MIR_BNE, label(floating), kind(node.left), imm(Integer));
            if (node.operation == Operation::Neg) {
                emit(MIR_SUBO, reg(integer_), imm(0), integer(node.left));
                emit(MIR_BO, label(overflow_));
            } else emit(MIR_MOV, reg(integer_), integer(node.left));
            store_integer(index);
            jump(done);
            mark(floating);
            emit(node.operation == Operation::Neg ? MIR_DNEG : MIR_DMOV, reg(number_), number(node.left));
            store_number(index);
            mark(done);
            return;
        }
        if (node.right != node.left) emit_node(node.right);
        if (node.operation == Operation::Compare || node.operation == Operation::Function) {
            host_request(index);
            return;
        }
        numeric_check(node.left);
        numeric_check(node.right);
        auto floating = MIR_new_label(ctx_), done = MIR_new_label(ctx_);
        if (node.operation != Operation::Div) {
            emit(MIR_BNE, label(floating), kind(node.left), imm(Integer));
            emit(MIR_BNE, label(floating), kind(node.right), imm(Integer));
            auto opcode = node.operation == Operation::Add ? MIR_ADDO :
                          node.operation == Operation::Sub ? MIR_SUBO : MIR_MULO;
            emit(opcode, reg(integer_), integer(node.left), integer(node.right));
            emit(MIR_BO, label(overflow_));
            store_integer(index);
            jump(done);
        }
        mark(floating);
        if (node.operation == Operation::Div)
            emit(MIR_DBEQ, label(divide_), number(node.right), real(0));
        auto opcode = node.operation == Operation::Add ? MIR_DADD :
                      node.operation == Operation::Sub ? MIR_DSUB :
                      node.operation == Operation::Mul ? MIR_DMUL : MIR_DDIV;
        emit(opcode, reg(number_), number(node.left), number(node.right));
        finite_check();
        store_number(index);
        mark(done);
    }
public:
    Generator(MirOwner& owner, Syntax const& syntax) : ctx_(owner.context), syntax_(syntax) {
        auto module = MIR_new_module(ctx_, "rulesforge_scalar");
        load_ = helper("rf_scalar_cached", reinterpret_cast<void*>(load_cached), 2);
        truth_ = helper("rf_scalar_truth", reinterpret_cast<void*>(truth), 1);
        finite_ = helper("rf_scalar_finite", reinterpret_cast<void*>(finite), 0, true);
        auto result = MIR_T_I64;
        MIR_var_t argument{};
        argument.type = MIR_T_P; argument.name = "frame";
        function_ = MIR_new_func_arr(ctx_, "evaluate", 1, &result, 1, &argument);
        frame_ = MIR_reg(ctx_, "frame", function_->u.func);
        status_ = MIR_new_func_reg(ctx_, function_->u.func, MIR_T_I64, "status");
        flag_ = MIR_new_func_reg(ctx_, function_->u.func, MIR_T_I64, "flag");
        integer_ = MIR_new_func_reg(ctx_, function_->u.func, MIR_T_I64, "integer");
        number_ = MIR_new_func_reg(ctx_, function_->u.func, MIR_T_D, "number");
        wrong_type_ = MIR_new_label(ctx_);
        divide_ = MIR_new_label(ctx_); overflow_ = MIR_new_label(ctx_);
        nonfinite_ = MIR_new_label(ctx_);
        auto dispatch = MIR_new_label(ctx_), start = MIR_new_label(ctx_);
        continuations_.push_back(start);
        jump(dispatch);
        mark(start);
        emit_node(syntax.root);
        emit(MIR_RET, imm(Ok));
        mark(wrong_type_); emit(MIR_RET, imm(WrongType));
        mark(divide_); emit(MIR_RET, imm(DivideByZero));
        mark(overflow_); emit(MIR_RET, imm(Overflow));
        mark(nonfinite_); emit(MIR_RET, imm(NonFinite));
        mark(dispatch);
        std::vector<MIR_op_t> destinations{continuation()};
        for (auto target : continuations_) destinations.push_back(label(target));
        MIR_append_insn(ctx_, function_,
            MIR_new_insn_arr(ctx_, MIR_SWITCH, destinations.size(), destinations.data()));
        MIR_finish_func(ctx_);
        MIR_finish_module(ctx_);
        MIR_load_module(ctx_, module);
        MIR_gen_init(ctx_);
        owner.generator = true;
        MIR_link(ctx_, MIR_set_gen_interface, nullptr);
    }
    void* generate() { return MIR_gen(ctx_, function_); }
};
} // namespace

struct ExpressionDescriptor::Impl {
    std::string original_expr;
    Syntax syntax;
    MirOwner owner;
    using Entry = int64_t (*)(Frame*);
    Entry entry;
    explicit Impl(std::string source)
        : original_expr(std::move(source)), syntax(Parser(original_expr).parse()) {
        Generator generator(owner, syntax);
        entry = reinterpret_cast<Entry>(generator.generate());
        if (!entry) throw std::runtime_error("MIR failed to generate scalar expression");
    }
};
ExpressionDescriptor::ExpressionDescriptor(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {}
std::shared_ptr<ExpressionDescriptor> ExpressionDescriptor::compile_required(std::string const& source) {
    if (source.empty()) throw std::invalid_argument("Empty expression");
    if (source.size() > max_source_bytes) throw std::length_error("Scalar expression source limit exceeded");
    auto impl = std::make_shared<Impl>(source);
    return std::shared_ptr<ExpressionDescriptor>(new ExpressionDescriptor(std::move(impl)));
}
std::shared_ptr<ExpressionDescriptor> ExpressionDescriptor::compile(std::string const& source,
                                                                   std::string* error) {
    try {
        auto program = compile_required(source);
        if (error) error->clear();
        return program;
    } catch (std::logic_error const& failure) {
        if (error) *error = failure.what();
        return nullptr;
    }
}
std::vector<std::string> const& ExpressionDescriptor::variables() const { return impl_->syntax.names; }
std::string const& ExpressionDescriptor::expression_string() const { return impl_->original_expr; }
std::optional<ConstraintValue> ExpressionDescriptor::evaluate(Resolver resolver, void* context) const {
    if (!resolver && !impl_->syntax.variables.empty())
        throw std::invalid_argument("Scalar expression requires a variable resolver");
    State state{impl_->syntax.variables};
    Frame frame{};
    frame.state = &state;
    for (;;) switch (impl_->entry(&frame)) {
        case Ok: return unpack(frame.slots[impl_->syntax.root]);
        case Missing: return std::nullopt;
        case HostRequest: {
            auto const index = static_cast<size_t>(frame.request);
            auto const& node = impl_->syntax.nodes[index];
            switch (node.operation) {
                case Operation::Variable: {
                    auto& value = state.values[node.argument];
                    value = resolver(context, state.variables[node.argument]);
                    if (!value) return std::nullopt;
                    auto packed = pack(*value);
                    state.cached[node.argument] = packed;
                    frame.slots[index] = packed;
                    break;
                }
                case Operation::Compare:
                    compare(&frame, index, node.argument, node.left, node.right);
                    break;
                case Operation::Function:
                    if (function(&frame, index, node.argument, node.left, node.right) == WrongType)
                        throw std::invalid_argument("Scalar function requires numeric operands");
                    break;
                default: throw std::logic_error("Invalid MIR host request");
            }
            break;
        }
        case WrongType: throw std::invalid_argument("Scalar arithmetic requires numeric operands");
        case DivideByZero: throw std::domain_error("Scalar division by zero");
        case Overflow: throw std::overflow_error("Scalar integer arithmetic overflow");
        case NonFinite: throw std::domain_error("Non-finite scalar arithmetic result");
        default: throw std::logic_error("Invalid MIR scalar status");
    }
}
bool ExpressionDescriptor::truthy(ConstraintValue const& value) {
    if (auto v = std::get_if<bool>(&value)) return *v;
    if (auto v = std::get_if<int64_t>(&value)) return *v != 0;
    if (auto v = std::get_if<uint64_t>(&value)) return *v != 0;
    if (auto v = std::get_if<double>(&value)) return std::fpclassify(*v) != FP_ZERO;
    if (auto v = std::get_if<DurationValue>(&value)) return v->milliseconds != 0;
    if (auto v = std::get_if<std::string>(&value)) return !v->empty() && *v != "false" && *v != "0";
    return !std::holds_alternative<NilValue>(value);
}
} // namespace rulesforge
