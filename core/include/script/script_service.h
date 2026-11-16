#pragma once
#include "script/script_types.h"
#include <cmeta/service_host_provider_abi.h>

/* Exact pointer identities keep the canonical Interface signature typed across
 * the DSO. The shared declaration is the only native request layout. */
#define RF_SCRIPT_POINTER(Type)                                                                    \
  static const cmeta_type_identity Type##_identity = CMETA_TYPE_ID_ATOM_INIT("rulesforge." #Type); \
  static const cmeta_type_identity Type##_pointer_identity =                                       \
      CMETA_TYPE_ID_POINTER_INIT(&Type##_identity);                                                \
  static const cmeta_type_desc Type##_type = {                                                     \
      #Type, sizeof(Type), CMETA_ALIGNOF(Type), CMETA_T_OBJECT, NULL, NULL, &Type##_identity};     \
  static const cmeta_type_desc Type##_pointer_type = {                                             \
      #Type " *",   sizeof(Type *), CMETA_ALIGNOF(Type *),   CMETA_T_POINTER,                      \
      &Type##_type, NULL,           &Type##_pointer_identity}
RF_SCRIPT_POINTER(rf_script_open);
RF_SCRIPT_POINTER(rf_script_call);
RF_SCRIPT_POINTER(rf_script_close);
#undef RF_SCRIPT_POINTER

#define RF_SCRIPT_METHODS(X, I)                                                                    \
  X(I, FR1, int, open, stateful, &cmeta_type_int, CMETA_ABI_SCALAR, CMETA_RESULT_VALUE,            \
    (rf_script_open *, request, CMETA_PARAM_IN | CMETA_PARAM_OUT | CMETA_PARAM_BORROWED,           \
     &rf_script_open_pointer_type, CMETA_ABI_OBJECT_POINTER))                                      \
  X(I, FR1, int, invoke, stateful, &cmeta_type_int, CMETA_ABI_SCALAR, CMETA_RESULT_VALUE,          \
    (rf_script_call *, request, CMETA_PARAM_IN | CMETA_PARAM_OUT | CMETA_PARAM_BORROWED,           \
     &rf_script_call_pointer_type, CMETA_ABI_OBJECT_POINTER))                                      \
  X(I, FR1, int, close, stateful, &cmeta_type_int, CMETA_ABI_SCALAR, CMETA_RESULT_VALUE,           \
    (rf_script_close *, request, CMETA_PARAM_IN | CMETA_PARAM_OUT | CMETA_PARAM_BORROWED,          \
     &rf_script_close_pointer_type, CMETA_ABI_OBJECT_POINTER))
CMETA_INTERFACE(rf_script_service, RF_SCRIPT_METHODS);
CMETA_OBJECT_INTERFACE_ADAPTER(rf_script_service);

#define RF_SCRIPT_PROVIDER_EXPORT "rulesforge.toad.provider"
