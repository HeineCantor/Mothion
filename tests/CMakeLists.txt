find_package(Qt6 REQUIRED COMPONENTS Test)

# mothion_add_test(<name> <libs...>): one QtTest executable per test file.
function(mothion_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

mothion_add_test(tst_core mothion_core)
mothion_add_test(tst_canvas mothion_gui)
