find_package(Qt6 REQUIRED COMPONENTS Test)

# mothion_add_test(<name> <libs...>): one QtTest executable per test file.
function(mothion_add_test name)
    qt_add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARGN})
    add_test(NAME ${name} COMMAND ${name})
    # Plugins from the Qt installation: windeployqt puts a partial plugins
    # folder next to the executables (no offscreen platform) that would win otherwise.
    set_tests_properties(${name} PROPERTIES ENVIRONMENT
        "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}")
endfunction()

mothion_add_test(tst_core mothion_core)
mothion_add_test(tst_brush mothion_core)
mothion_add_test(tst_puppet mothion_core)
mothion_add_test(tst_document mothion_core)
mothion_add_test(tst_blend mothion_core)
mothion_add_test(tst_tween mothion_core)
mothion_add_test(tst_tweendoc mothion_gui)
mothion_add_test(tst_camera mothion_core)
mothion_add_test(tst_cameradoc mothion_gui)
mothion_add_test(tst_commands mothion_core)
mothion_add_test(tst_canvas mothion_gui)
mothion_add_test(tst_compositing mothion_gui)
mothion_add_test(tst_colorsets mothion_gui)
mothion_add_test(tst_autosave mothion_gui Qt6::Sql)
mothion_add_test(tst_diagnostics mothion_gui Qt6::Sql)
find_package(Qt6 REQUIRED COMPONENTS Sql)
mothion_add_test(tst_brushimport mothion_gui Qt6::Sql)
mothion_add_test(tst_projectfile mothion_gui Qt6::Sql)
find_package(Qt6 REQUIRED COMPONENTS Network)
# The UI tests, one executable for each area (they run side by side under ctest -j, and a build only redoes the
# area that changed). The headers they all need are in test_common.h. A fake Discord for the Rich Presence
# is in the window ones, so they link Network.
foreach(area panels tools drawing puppet timeline audio compositing window)
    mothion_add_test(tst_ui_${area} mothion_gui Qt6::Network)
endforeach()
