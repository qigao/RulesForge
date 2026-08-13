find_package(cxxopts REQUIRED)

add_executable(capi_demo capi_demo.cpp)
target_link_libraries(capi_demo PRIVATE rules_forge cxxopts::cxxopts TurboParser::Parser TurboUtils::Core)
set_target_properties(capi_demo PROPERTIES FOLDER capi/examples)

# Copy sample files to output directory
configure_file(payments.rfl ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/payments.rfl COPYONLY)
configure_file(payments.schema ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/payments.schema COPYONLY)
configure_file(payments_test_data.csv ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/payments_test_data.csv COPYONLY)
configure_file(payments_test_data.json ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/payments_test_data.json COPYONLY)

if(BUILD_TESTING OR ENABLE_TESTS)
  add_test(
    NAME capi_demo_json_smoke
    COMMAND
      $<TARGET_FILE:capi_demo>
      -r payments.rfl
      -s payments.schema
      -j payments_test_data.json
      -m testOrders:Order
      -q OrdersWithDiscount
      -b order
      -f quantity,unitPrice,finalPrice
  )

  add_test(
    NAME capi_demo_csv_smoke
    COMMAND
      $<TARGET_FILE:capi_demo>
      -r payments.rfl
      -s payments.schema
      -c payments_test_data.csv
      -T Order
      -q OrdersWithDiscount
      -b order
      -f quantity,unitPrice,finalPrice
  )

  add_test(
    NAME capi_demo_observability_smoke
    COMMAND
      $<TARGET_FILE:capi_demo>
      -r payments.rfl
      -s payments.schema
      -c payments_test_data.csv
      -T Order
      --validation warn
      --trace
      --memory
      -q OrdersWithDiscount
      -b order
      -f quantity,finalPrice
  )

  set_tests_properties(capi_demo_json_smoke capi_demo_csv_smoke capi_demo_observability_smoke PROPERTIES
    WORKING_DIRECTORY $<TARGET_FILE_DIR:capi_demo>
  )
endif()
