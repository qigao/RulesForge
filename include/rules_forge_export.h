#ifndef RULES_FORGE_EXPORT_H
#define RULES_FORGE_EXPORT_H

/* RulesForge owns its ABI marker. TurboUtils' TURBO_API only describes
 * TurboUtils libraries and must not leak their producer/consumer state here. */
#ifndef RULES_FORGE_API
#  if defined(_WIN32) && defined(RULES_FORGE_BUILD)
#    define RULES_FORGE_API __declspec(dllexport)
#  elif !defined(_WIN32) && defined(__GNUC__) && __GNUC__ >= 4
#    define RULES_FORGE_API __attribute__((visibility("default")))
#  else
#    define RULES_FORGE_API
#  endif
#endif

#ifndef RULES_FORGE_C_API
#  ifdef __cplusplus
#    define RULES_FORGE_C_API extern "C" RULES_FORGE_API
#  else
#    define RULES_FORGE_C_API RULES_FORGE_API
#  endif
#endif

#endif /* RULES_FORGE_EXPORT_H */
