set(SNMPB_APP_SOURCES
    appearance.cpp
    agent.cpp
    agentrequestselection.cpp
    connectionbar.cpp
    connectionmodel.cpp
    comboboxes.cpp
    communitycredentialrepository.cpp
    communitycredentialservice.cpp
    communitycredentialmanager.cpp
    credentialsdialog.cpp
    credentialrecords.cpp
    diagnosticlogger.cpp
    mainwindowlayout.cpp
    startupcoordinator.cpp
    workbenchshell.cpp
    productidentity.cpp
    logsnmpb.cpp
    markerwidget.cpp
    mibeditor.cpp
    mibhighlighter.cpp
    miblibrary.cpp
    miblegacymigration.cpp
    mibdependencyindex.cpp
    mibeffectiveplan.cpp
    mibenvironment.cpp
    mibenvironmentextractor.cpp
    mibenvironmentregistry.cpp
    mibenvironmentcache.cpp
    mibenvironmentmanager.cpp
    mibengine.cpp
    mibenginevalidation.cpp
    mibruntimeparser.cpp
    mibruntimestage.cpp
    mibvaluesemantics.cpp
    snmpreport.cpp
    mibcollectionindex.cpp
    mibcollectionservice.cpp
    requestlog.cpp
    profilematch.cpp
    profiledetector.cpp
    queryresultspanel.cpp
    requestlogpanel.cpp
    collectionsearchpanel.cpp
    watchcalculator.cpp
    watchservice.cpp
    watchpanel.cpp
    mibprofile.cpp
    mibdownloadtransport.cpp
    miblibrarywidget.cpp
    mibmodule.cpp
    mibcandidatefilter.cpp
    mibcollection.cpp
    mibdiagnosticcollector.cpp
    mibdiagnosticmodel.cpp
    mibservice.cpp
    mibtreemodel.cpp
    mibnode.cpp
    mibmodelview.cpp
    mibselection.cpp
    mibtextedit.cpp
    mibview.cpp
    preferences.cpp
    preferencesettings.cpp
    snmprequestconfig.cpp
    snmprequestconfigadapter.cpp
    snmprequestcontext.cpp
    snmpinstanceoperation.cpp
    snmpinstanceasyncrunner.cpp
    snmptransport.cpp
    snmptableoperation.cpp
    snmptableasyncrunner.cpp
    snmpwalkoperation.cpp
    snmpwalkasyncrunner.cpp
    snmpsingleoperation.cpp
    snmpsingleasyncrunner.cpp
    snmpquerytext.cpp
    tablenodevalidation.cpp
    tabletraversal.cpp
    tableindex.cpp
    snmpb.cpp
    trap.cpp
    trapdecoder.cpp
    traphistorystore.cpp
    trappresenter.cpp
    trapservice.cpp
    usmprofile.cpp
    usmcredentialrepository.cpp
    usmcredentialcoordinator.cpp
    usmcredentialruntime.cpp
    usmcredentialservice.cpp
    udpportowner.cpp
    main.cpp
)

set(SNMPB_APP_HEADERS
    appearance.h
    appearancecolors.h
    themedtextedit.h
    agent.h
    agentrequestselection.h
    connectionbar.h
    connectionmodel.h
    snmprequestsettings.h
    comboboxes.h
    communitycredentialrepository.h
    communitycredentialservice.h
    communitycredentialmanager.h
    credentialsdialog.h
    credentialrecords.h
    diagnosticlogger.h
    mainwindowlayout.h
    startupcoordinator.h
    workbenchshell.h
    productidentity.h
    logsnmpb.h
    markerwidget.h
    mibeditor.h
    mibhighlighter.h
    miblibrary.h
    miblegacymigration.h
    mibdependencyindex.h
    mibeffectiveplan.h
    mibenvironment.h
    mibenvironmentextractor.h
    mibenvironmentregistry.h
    mibenvironmentcache.h
    mibenvironmentmanager.h
    mibengine.h
    mibruntimeparser.h
    mibruntimestage.h
    mibservice_internal.h
    mibvaluesemantics.h
    snmpreport.h
    mibcollectionindex.h
    mibcollectionservice.h
    requestlog.h
    profilematch.h
    profiledetector.h
    queryresultspanel.h
    requestlogpanel.h
    collectionsearchpanel.h
    watchcalculator.h
    watchservice.h
    watchpanel.h
    mibprofile.h
    mibdownloadtransport.h
    miblibrarywidget.h
    mibmodule.h
    mibcandidatefilter.h
    mibcollection.h
    mibdiagnosticcollector.h
    mibdiagnosticmodel.h
    mibrecords.h
    mibservice.h
    mibtreemodel.h
    mibnode.h
    mibmodelview.h
    mibselection.h
    mibtextedit.h
    mibview.h
    preferences.h
    preferencesettings.h
    snmprequestconfig.h
    snmprequestconfigadapter.h
    snmprequestcontext.h
    snmpinstanceoperation.h
    snmpinstanceasyncrunner.h
    snmptransport.h
    snmptableoperation.h
    snmptableasyncrunner.h
    snmpwalkoperation.h
    snmpwalkasyncrunner.h
    snmpsingleoperation.h
    snmpsingleasyncrunner.h
    snmpquerytext.h
    tablenodevalidation.h
    tabletraversal.h
    tableindex.h
    snmpb.h
    snmpbapp.h
    trap.h
    trapdecoder.h
    traphistorystore.h
    trappresenter.h
    traprecord.h
    trapservice.h
    usmprofile.h
    usmcredentialrepository.h
    usmcredentialcoordinator.h
    usmcredentialruntime.h
    usmcredentialservice.h
    udpportowner.h
)

set(SNMPB_APP_FORMS
    find.ui
    gotoline.ui
    mainw.ui
    preferences.ui
    replace.ui
    usmprofile.ui
    varbinds.ui
)

if(WIN32)
    list(APPEND SNMPB_APP_SOURCES snmpb.rc)
endif()

add_executable(snmpb
    ${SNMPB_APP_SOURCES}
    ${SNMPB_APP_HEADERS}
    ${SNMPB_APP_FORMS}
    snmpb.qrc
    appearance.qrc
)

set_target_properties(snmpb PROPERTIES
    OUTPUT_NAME "mib-navigator"
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON
    AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "MIB Navigator"
)
if(APPLE)
    set_target_properties(snmpb PROPERTIES OUTPUT_NAME "MIB Navigator")
endif()

if(UNIX AND NOT APPLE)
    target_compile_definitions(snmpb
        PRIVATE
            SNMPB_INSTALL_DATA_RELATIVE_PATH="../${CMAKE_INSTALL_DATADIR}/snmpb"
    )
endif()

target_include_directories(snmpb
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${PROJECT_BINARY_DIR}/generated
)

target_link_libraries(snmpb
    PRIVATE
        Qt6::Core
        Qt6::Widgets
        Qt6::Network
        Qt6::Svg
        snmpb_libsmi
        snmpb_snmp_pp
        snmpb_libtomcrypt
)
if(WIN32)
    target_link_libraries(snmpb PRIVATE iphlpapi ws2_32)
endif()
if(MSVC)
    # Retain release optimization while producing symbols for field crash
    # diagnosis. /OPT options preserve the normal optimized Release layout.
    target_link_options(snmpb PRIVATE
        $<$<CONFIG:Release>:/DEBUG>
        $<$<CONFIG:Release>:/OPT:REF>
        $<$<CONFIG:Release>:/OPT:ICF>)
endif()

find_package(Qt6 6.10 REQUIRED COMPONENTS LinguistTools)

qt_add_translations(snmpb
    TS_FILES
        l10n/snmpb.uk_UA.ts
    LRELEASE_OPTIONS
        -removeidentical
        -compress
    RESOURCE_PREFIX
        /i18n
)

add_custom_command(TARGET snmpb POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DSNMPB_SOURCE_DIR=${PROJECT_SOURCE_DIR}
        -DSNMPB_RUNTIME_DATA_DIR=$<TARGET_FILE_DIR:snmpb>
        -P ${PROJECT_SOURCE_DIR}/cmake/StageSnmpbMibs.cmake
    COMMENT "Staging bundled MIBs and PIBs beside MIB Navigator"
    VERBATIM
)

file(GLOB SNMPB_BUNDLED_MIB_FILES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/libsmi/mibs/iana/*
    ${PROJECT_SOURCE_DIR}/libsmi/mibs/ietf/*
    ${PROJECT_SOURCE_DIR}/libsmi/mibs/tubs/*
)
file(GLOB SNMPB_BUNDLED_PIB_FILES CONFIGURE_DEPENDS
    ${PROJECT_SOURCE_DIR}/libsmi/pibs/ietf/*
    ${PROJECT_SOURCE_DIR}/libsmi/pibs/tubs/*
)
list(FILTER SNMPB_BUNDLED_MIB_FILES EXCLUDE REGEX "/Makefile[^/]*$")
list(FILTER SNMPB_BUNDLED_MIB_FILES EXCLUDE REGEX "-orig$")
list(FILTER SNMPB_BUNDLED_PIB_FILES EXCLUDE REGEX "/Makefile[^/]*$")
list(FILTER SNMPB_BUNDLED_PIB_FILES EXCLUDE REGEX "-orig$")

install(TARGETS snmpb
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    BUNDLE DESTINATION .
)

if(WIN32 OR APPLE)
    qt_generate_deploy_app_script(
        TARGET snmpb
        OUTPUT_SCRIPT SNMPB_QT_DEPLOY_SCRIPT
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${SNMPB_QT_DEPLOY_SCRIPT})
endif()

if(WIN32 AND TARGET Qt6::QOffscreenIntegrationPlugin)
    # The installed launch smoke deliberately runs without a desktop. Qt's
    # deployment scanner omits this non-default platform plugin unless it is
    # requested explicitly.
    install(FILES $<TARGET_FILE:Qt6::QOffscreenIntegrationPlugin>
        DESTINATION plugins/platforms)
endif()

if(WIN32)
    set(SNMPB_INSTALL_DATA_DIR ${CMAKE_INSTALL_BINDIR})
elseif(APPLE)
    set(SNMPB_INSTALL_DATA_DIR "MIB Navigator.app/Contents/Resources")
else()
    set(SNMPB_INSTALL_DATA_DIR "${CMAKE_INSTALL_DATADIR}/snmpb")
endif()

install(FILES ${SNMPB_BUNDLED_MIB_FILES}
    DESTINATION "${SNMPB_INSTALL_DATA_DIR}/mibs"
)
install(FILES ${SNMPB_BUNDLED_PIB_FILES}
    DESTINATION "${SNMPB_INSTALL_DATA_DIR}/pibs"
)

if(BUILD_TESTING)
    add_executable(snmpb_mib_engine_test mibengine.cpp mibengine.h mibengine_test.cpp)
    target_compile_definitions(snmpb_mib_engine_test PRIVATE SNMPB_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(snmpb_mib_engine_test PRIVATE Qt6::Core)
    add_test(NAME snmpb.mib-engine COMMAND $<TARGET_FILE:snmpb_mib_engine_test>)

    add_executable(snmpb_mib_environment_manager_test
        mibengine.cpp mibengine.h mibenvironment.cpp mibenvironment.h
        mibenvironmentregistry.cpp mibenvironmentregistry.h
        mibenvironmentcache.cpp mibenvironmentcache.h
        mibenvironmentmanager.cpp mibenvironmentmanager.h
        mibenvironmentmanager_test.cpp)
    target_link_libraries(snmpb_mib_environment_manager_test PRIVATE Qt6::Core)
    set_target_properties(snmpb_mib_environment_manager_test PROPERTIES AUTOMOC ON)
    add_test(NAME snmpb.mib-environment-manager COMMAND $<TARGET_FILE:snmpb_mib_environment_manager_test>)

    function(snmpb_configure_qt_test_runtime)
        foreach(test_name IN LISTS ARGN)
            # Checked on every platform: only Windows uses the list, so a stale name used to
            # surface only in Windows CI.
            if(NOT TEST ${test_name})
                message(FATAL_ERROR "snmpb_configure_qt_test_runtime: no test named ${test_name}")
            endif()
        endforeach()
        if(WIN32)
            foreach(test_name IN LISTS ARGN)
                set_property(TEST ${test_name} PROPERTY ENVIRONMENT
                    "PATH=$<TARGET_FILE_DIR:Qt6::Core>\;$ENV{PATH}")
            endforeach()
        endif()
    endfunction()

    add_executable(snmpb_mib_dependency_index_test
        mibcandidatefilter.cpp mibcandidatefilter.h
        miblibrary.cpp miblibrary.h mibcollection.cpp mibcollection.h
        mibdependencyindex.cpp mibdependencyindex.h mibdependencyindex_test.cpp)
    target_link_libraries(snmpb_mib_dependency_index_test PRIVATE Qt6::Core)
    if(WIN32)
        add_test(NAME snmpb.mib-dependency-index COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_mib_dependency_index_test>)
    else()
        add_test(NAME snmpb.mib-dependency-index COMMAND $<TARGET_FILE:snmpb_mib_dependency_index_test>)
    endif()

    add_executable(snmpb_mib_effective_plan_test
        mibengine.cpp mibengine.h
        mibruntimeparser.cpp mibruntimeparser.h
        mibruntimestage.cpp mibruntimestage.h
        mibenvironment.cpp mibenvironment.h
        mibenvironmentextractor.cpp mibenvironmentextractor.h
        mibcandidatefilter.cpp mibcandidatefilter.h
        miblibrary.cpp miblibrary.h mibcollection.cpp mibcollection.h
        mibprofile.cpp mibprofile.h
        mibdependencyindex.cpp mibdependencyindex.h
        mibeffectiveplan.cpp mibeffectiveplan.h mibeffectiveplan_test.cpp)
    target_link_libraries(snmpb_mib_effective_plan_test PRIVATE Qt6::Core snmpb_libsmi)
    if(WIN32)
        add_test(NAME snmpb.mib-effective-plan COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_mib_effective_plan_test>)
    else()
        add_test(NAME snmpb.mib-effective-plan COMMAND $<TARGET_FILE:snmpb_mib_effective_plan_test>)
    endif()

    add_executable(snmpb_mib_effective_resolver_test
        mibruntimestage.cpp mibruntimestage.h
        mibcandidatefilter.cpp mibcandidatefilter.h
        miblibrary.cpp miblibrary.h mibcollection.cpp mibcollection.h
        mibprofile.cpp mibprofile.h
        mibdependencyindex.cpp mibdependencyindex.h
        mibeffectiveplan.cpp mibeffectiveplan.h mibeffectiveresolver_test.cpp)
    target_link_libraries(snmpb_mib_effective_resolver_test PRIVATE Qt6::Core)
    add_test(NAME snmpb.mib-effective-resolver
        COMMAND $<TARGET_FILE:snmpb_mib_effective_resolver_test>)
    snmpb_configure_qt_test_runtime(snmpb.mib-effective-resolver)

    add_executable(snmpb_mib_legacy_migration_test
        mibcandidatefilter.cpp mibcandidatefilter.h
        miblibrary.cpp miblibrary.h mibcollection.cpp mibcollection.h
        mibdependencyindex.cpp mibdependencyindex.h
        mibprofile.cpp mibprofile.h
        miblegacymigration.cpp miblegacymigration.h miblegacymigration_test.cpp)
    target_link_libraries(snmpb_mib_legacy_migration_test PRIVATE Qt6::Core)
    add_test(NAME snmpb.mib-legacy-migration COMMAND $<TARGET_FILE:snmpb_mib_legacy_migration_test>)
    snmpb_configure_qt_test_runtime(snmpb.mib-legacy-migration)

    add_executable(snmpb_mib_dependency_acceptance
        mibcandidatefilter.cpp mibcandidatefilter.h
        miblibrary.cpp miblibrary.h mibcollection.cpp mibcollection.h
        mibdependencyindex.cpp mibdependencyindex.h mibdependencyacceptance.cpp)
    target_link_libraries(snmpb_mib_dependency_acceptance PRIVATE Qt6::Core snmpb_libsmi)

    add_executable(snmpb_mib_startup_path_test
        mibcandidatefilter.cpp mibcandidatefilter.h
        miblibrary.cpp miblibrary.h mibcollection.cpp mibcollection.h
        mibdependencyindex.cpp mibdependencyindex.h mibstartuppath_test.cpp)
    target_link_libraries(snmpb_mib_startup_path_test PRIVATE Qt6::Core)
    if(WIN32)
        add_test(NAME snmpb.mib-startup-path COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_mib_startup_path_test>)
    else()
        add_test(NAME snmpb.mib-startup-path COMMAND $<TARGET_FILE:snmpb_mib_startup_path_test>)
    endif()

    add_executable(snmpb_stability_diagnostics_test
        diagnosticlogger.cpp diagnosticlogger.h
        udpportowner.cpp udpportowner.h
        stabilitydiagnostics_test.cpp)
    target_link_libraries(snmpb_stability_diagnostics_test PRIVATE Qt6::Core Qt6::Widgets)
    if(WIN32)
        target_link_libraries(snmpb_stability_diagnostics_test PRIVATE iphlpapi ws2_32)
        add_test(NAME snmpb.stability-diagnostics COMMAND ${CMAKE_COMMAND} -E env
            "QT_QPA_PLATFORM=offscreen"
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_stability_diagnostics_test>)
    else()
        add_test(NAME snmpb.stability-diagnostics COMMAND ${CMAKE_COMMAND} -E env
            "QT_QPA_PLATFORM=offscreen"
            $<TARGET_FILE:snmpb_stability_diagnostics_test>)
    endif()

    add_executable(snmpb_product_identity_test
        productidentity.cpp productidentity.h productidentity_test.cpp)
    target_include_directories(snmpb_product_identity_test PRIVATE
        ${PROJECT_BINARY_DIR}/generated)
    target_link_libraries(snmpb_product_identity_test PRIVATE Qt6::Core)
    if(WIN32)
        add_test(NAME snmpb.product-identity COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_product_identity_test>)
    else()
        add_test(NAME snmpb.product-identity COMMAND
            $<TARGET_FILE:snmpb_product_identity_test>)
    endif()

    add_executable(snmpb_mib_service_test
        mibengine.cpp mibengine.h
        mibdiagnosticcollector.cpp mibdiagnosticcollector.h
        mibdiagnosticmodel.cpp mibdiagnosticmodel.h
        mibcandidatefilter.cpp mibcandidatefilter.h
        mibrecords.h mibservice.cpp mibservice.h
        mibtreemodel.cpp mibtreemodel.h
        tablenodevalidation.cpp tablenodevalidation.h
        mibmodelview.cpp mibmodelview.h mibservice_test.cpp)
    set_target_properties(snmpb_mib_service_test PROPERTIES AUTOMOC ON)
    target_compile_definitions(snmpb_mib_service_test PRIVATE
        SNMPB_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(snmpb_mib_service_test PRIVATE
        Qt6::Core Qt6::Widgets snmpb_libsmi)
    if(WIN32)
        add_test(NAME snmpb.mib-service COMMAND ${CMAKE_COMMAND} -E env
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_mib_service_test>)
    else()
        add_test(NAME snmpb.mib-service COMMAND ${CMAKE_COMMAND} -E env
            "QT_QPA_PLATFORM=offscreen"
            $<TARGET_FILE:snmpb_mib_service_test>)
    endif()

    add_executable(snmpb_mib_environment_test
        mibengine.cpp mibengine.h
        mibenvironment.cpp mibenvironment.h
        mibenvironmentextractor.cpp mibenvironmentextractor.h
        mibenvironmentregistry.cpp mibenvironmentregistry.h
        mibeffectiveplan.h mibdependencyindex.h mibprofile.h
        mibenvironment_test.cpp)
    target_compile_definitions(snmpb_mib_environment_test PRIVATE
        SNMPB_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(snmpb_mib_environment_test PRIVATE Qt6::Core snmpb_libsmi)
    add_test(NAME snmpb.mib-environment COMMAND $<TARGET_FILE:snmpb_mib_environment_test>)
    add_test(NAME snmpb.mib-environment-large
        COMMAND $<TARGET_FILE:snmpb_mib_environment_test> --large)

    add_executable(snmpb_mib_value_check_test
        mibenvironment.cpp mibenvironment.h
        mibvaluesemantics.cpp mibvaluesemantics.h
        mibvaluecheck_test.cpp)
    target_link_libraries(snmpb_mib_value_check_test PRIVATE Qt6::Core snmpb_snmp_pp snmpb_libtomcrypt)
    if(WIN32)
        add_test(NAME snmpb.mib-value-check COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_mib_value_check_test>)
    else()
        add_test(NAME snmpb.mib-value-check COMMAND $<TARGET_FILE:snmpb_mib_value_check_test>)
    endif()

    add_executable(snmpb_watch_calculator_test watchcalculator.cpp watchcalculator.h watchcalculator_test.cpp)
    target_link_libraries(snmpb_watch_calculator_test PRIVATE Qt6::Core)
    if(WIN32)
        add_test(NAME snmpb.watch-calculator COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_watch_calculator_test>)
    else()
        add_test(NAME snmpb.watch-calculator COMMAND $<TARGET_FILE:snmpb_watch_calculator_test>)
    endif()

    add_executable(snmpb_watch_service_test
        mibenvironment.cpp mibenvironment.h
        mibvaluesemantics.cpp mibvaluesemantics.h
        snmpreport.cpp snmpreport.h
        snmptransport.cpp snmptransport.h
        snmprequestconfig.cpp snmprequestconfig.h
        snmprequestconfigadapter.cpp snmprequestconfigadapter.h
        watchcalculator.cpp watchcalculator.h
        watchservice.cpp watchservice.h
        watchservice_test.cpp)
    set_target_properties(snmpb_watch_service_test PROPERTIES AUTOMOC ON)
    target_link_libraries(snmpb_watch_service_test PRIVATE Qt6::Core snmpb_snmp_pp snmpb_libtomcrypt)
    if(WIN32)
        add_test(NAME snmpb.watch-service COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_watch_service_test>)
    else()
        add_test(NAME snmpb.watch-service COMMAND $<TARGET_FILE:snmpb_watch_service_test>)
    endif()

    add_executable(snmpb_mib_collection_index_test mibcollectionindex.cpp mibcollectionindex.h mibcollectionindex_test.cpp)
    target_link_libraries(snmpb_mib_collection_index_test PRIVATE Qt6::Core)
    if(WIN32)
        add_test(NAME snmpb.mib-collection-index COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_mib_collection_index_test>)
    else()
        add_test(NAME snmpb.mib-collection-index COMMAND $<TARGET_FILE:snmpb_mib_collection_index_test>)
    endif()

    add_executable(snmpb_mib_collection_accuracy_test mibcollectionindex.cpp mibcollectionindex.h mibcollectionaccuracy_test.cpp)
    target_compile_definitions(snmpb_mib_collection_accuracy_test PRIVATE SNMPB_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(snmpb_mib_collection_accuracy_test PRIVATE Qt6::Core snmpb_libsmi)
    if(WIN32)
        add_test(NAME snmpb.mib-collection-accuracy COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_mib_collection_accuracy_test>)
    else()
        add_test(NAME snmpb.mib-collection-accuracy COMMAND $<TARGET_FILE:snmpb_mib_collection_accuracy_test>)
    endif()

    add_executable(snmpb_mib_collection_service_test
        mibcollectionindex.cpp mibcollectionindex.h
        mibcollectionservice.cpp mibcollectionservice.h
        mibcollectionservice_test.cpp)
    set_target_properties(snmpb_mib_collection_service_test PROPERTIES AUTOMOC ON)
    target_link_libraries(snmpb_mib_collection_service_test PRIVATE Qt6::Core)
    if(WIN32)
        add_test(NAME snmpb.mib-collection-service COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_mib_collection_service_test>)
    else()
        add_test(NAME snmpb.mib-collection-service COMMAND $<TARGET_FILE:snmpb_mib_collection_service_test>)
    endif()

    add_executable(snmpb_request_log_test requestlog.cpp requestlog.h requestlog_test.cpp)
    set_target_properties(snmpb_request_log_test PROPERTIES AUTOMOC ON)
    target_link_libraries(snmpb_request_log_test PRIVATE Qt6::Core Qt6::Gui)
    if(WIN32)
        add_test(NAME snmpb.request-log COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_request_log_test>)
    else()
        add_test(NAME snmpb.request-log COMMAND $<TARGET_FILE:snmpb_request_log_test>)
    endif()

    add_executable(snmpb_profile_match_test
        mibcollectionindex.cpp mibcollectionindex.h profilematch.cpp profilematch.h profilematch_test.cpp)
    target_link_libraries(snmpb_profile_match_test PRIVATE Qt6::Core)
    if(WIN32)
        add_test(NAME snmpb.profile-match COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_profile_match_test>)
    else()
        add_test(NAME snmpb.profile-match COMMAND $<TARGET_FILE:snmpb_profile_match_test>)
    endif()

    add_executable(snmpb_profile_detector_test
        snmpreport.cpp snmpreport.h snmptransport.cpp snmptransport.h snmprequestconfig.cpp snmprequestconfig.h
        snmprequestconfigadapter.cpp snmprequestconfigadapter.h
        profiledetector.cpp profiledetector.h profiledetector_test.cpp)
    set_target_properties(snmpb_profile_detector_test PROPERTIES AUTOMOC ON)
    target_link_libraries(snmpb_profile_detector_test PRIVATE Qt6::Core snmpb_snmp_pp snmpb_libtomcrypt)
    if(WIN32)
        add_test(NAME snmpb.profile-detector COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_profile_detector_test>)
    else()
        add_test(NAME snmpb.profile-detector COMMAND $<TARGET_FILE:snmpb_profile_detector_test>)
    endif()

    add_executable(snmpb_mib_automatic_profile_test
        mibcandidatefilter.cpp mibcandidatefilter.h
        miblibrary.cpp miblibrary.h mibcollection.cpp mibcollection.h mibprofile.cpp mibprofile.h
        mibdependencyindex.cpp mibdependencyindex.h mibeffectiveplan.cpp mibeffectiveplan.h
        mibrecords.h mibtreemodel.cpp mibtreemodel.h mibautomaticprofile_test.cpp)
    set_target_properties(snmpb_mib_automatic_profile_test PROPERTIES AUTOMOC ON)
    target_link_libraries(snmpb_mib_automatic_profile_test PRIVATE Qt6::Core)
    if(WIN32)
        add_test(NAME snmpb.mib-automatic-profile COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_mib_automatic_profile_test>)
    else()
        add_test(NAME snmpb.mib-automatic-profile COMMAND $<TARGET_FILE:snmpb_mib_automatic_profile_test>)
    endif()

    add_executable(snmpb_mib_profile_folder_test
        mibcandidatefilter.cpp mibcandidatefilter.h
        miblibrary.cpp miblibrary.h mibcollection.cpp mibcollection.h mibprofile.cpp mibprofile.h
        mibdependencyindex.cpp mibdependencyindex.h mibeffectiveplan.cpp mibeffectiveplan.h
        mibproblemreport.h mibproblem.h
        mibrecords.h mibtreemodel.cpp mibtreemodel.h mibprofilefolder_test.cpp)
    set_target_properties(snmpb_mib_profile_folder_test PROPERTIES AUTOMOC ON)
    target_link_libraries(snmpb_mib_profile_folder_test PRIVATE Qt6::Core snmpb_libsmi)
    if(WIN32)
        add_test(NAME snmpb.mib-profile-folders COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_mib_profile_folder_test>)
    else()
        add_test(NAME snmpb.mib-profile-folders COMMAND $<TARGET_FILE:snmpb_mib_profile_folder_test>)
    endif()

    add_executable(snmpb_snmp_report_test snmpreport.cpp snmpreport.h snmpreport_test.cpp)
    target_link_libraries(snmpb_snmp_report_test PRIVATE Qt6::Core snmpb_snmp_pp snmpb_libtomcrypt)
    if(WIN32)
        add_test(NAME snmpb.snmp-report COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_snmp_report_test>)
    else()
        add_test(NAME snmpb.snmp-report COMMAND $<TARGET_FILE:snmpb_snmp_report_test>)
    endif()

    add_executable(snmpb_mib_environment_consumer_test
        mibengine.cpp mibengine.h
        mibenvironment.cpp mibenvironment.h mibenvironmentextractor.cpp mibenvironmentextractor.h
        mibenvironmentregistry.cpp mibenvironmentregistry.h mibeffectiveplan.h
        trappresenter.cpp trappresenter.h traprecord.h
        mibvaluesemantics.cpp mibvaluesemantics.h
        snmprequestconfig.cpp snmprequestconfig.h snmprequestcontext.cpp snmprequestcontext.h
        mibnode.cpp mibnode.h mibview.cpp mibview.h mibtreemodel.cpp mibtreemodel.h
        mibmodelview.cpp mibmodelview.h mibrecords.h tablenodevalidation.cpp tablenodevalidation.h
        tabletraversal.cpp tabletraversal.h find.ui mibenvironmentconsumer_test.cpp)
    set_target_properties(snmpb_mib_environment_consumer_test PROPERTIES AUTOMOC ON AUTOUIC ON)
    target_compile_definitions(snmpb_mib_environment_consumer_test PRIVATE SNMPB_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(snmpb_mib_environment_consumer_test PRIVATE Qt6::Core Qt6::Widgets snmpb_libsmi snmpb_snmp_pp snmpb_libtomcrypt)
    if(WIN32)
        add_test(NAME snmpb.mib-environment-consumers COMMAND ${CMAKE_COMMAND} -E env
            "QT_QPA_PLATFORM=offscreen" "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_mib_environment_consumer_test>)
        add_test(NAME snmpb.mib-environment-transitions COMMAND ${CMAKE_COMMAND} -E env
            "QT_QPA_PLATFORM=offscreen" "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_mib_environment_consumer_test>)
        add_test(NAME snmpb.mib-environment-consumers-large COMMAND ${CMAKE_COMMAND} -E env
            "QT_QPA_PLATFORM=offscreen" "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_mib_environment_consumer_test> --large)
    else()
        add_test(NAME snmpb.mib-environment-consumers COMMAND ${CMAKE_COMMAND} -E env
            "QT_QPA_PLATFORM=offscreen" $<TARGET_FILE:snmpb_mib_environment_consumer_test>)
        add_test(NAME snmpb.mib-environment-transitions COMMAND ${CMAKE_COMMAND} -E env
            "QT_QPA_PLATFORM=offscreen" $<TARGET_FILE:snmpb_mib_environment_consumer_test>)
        add_test(NAME snmpb.mib-environment-consumers-large COMMAND ${CMAKE_COMMAND} -E env
            "QT_QPA_PLATFORM=offscreen" $<TARGET_FILE:snmpb_mib_environment_consumer_test> --large)
    endif()

    add_executable(snmpb_mib_library_test
        mibengine.cpp mibengine.h mibenginevalidation.cpp
        mibdownloadtransport.cpp mibdownloadtransport.h
        mibdiagnosticcollector.cpp mibdiagnosticcollector.h mibrecords.h
        mibcandidatefilter.cpp mibcandidatefilter.h
        miblibrary.cpp miblibrary.h mibcollection.cpp mibcollection.h miblibrary_test.cpp)
    set_target_properties(snmpb_mib_library_test PROPERTIES AUTOMOC ON)
    target_compile_definitions(snmpb_mib_library_test PRIVATE
        SNMPB_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(snmpb_mib_library_test PRIVATE
        Qt6::Core Qt6::Network snmpb_libsmi)
    if(WIN32)
        add_test(NAME snmpb.mib-library COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_mib_library_test>)
    else()
        add_test(NAME snmpb.mib-library COMMAND $<TARGET_FILE:snmpb_mib_library_test>)
    endif()

    add_executable(snmpb_mib_profile_test
        mibcandidatefilter.cpp mibcandidatefilter.h
        miblibrary.cpp miblibrary.h mibcollection.cpp mibcollection.h mibprofile.cpp mibprofile.h
        mibdependencyindex.cpp mibdependencyindex.h mibeffectiveplan.cpp mibeffectiveplan.h
        mibrecords.h mibtreemodel.cpp mibtreemodel.h mibprofile_test.cpp)
    set_target_properties(snmpb_mib_profile_test PROPERTIES AUTOMOC ON)
    target_link_libraries(snmpb_mib_profile_test PRIVATE Qt6::Core)
    if(WIN32)
        add_test(NAME snmpb.mib-profile COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_mib_profile_test>)
    else()
        add_test(NAME snmpb.mib-profile COMMAND $<TARGET_FILE:snmpb_mib_profile_test>)
    endif()

    add_executable(snmpb_mib_inventory_widget_test
        diagnosticlogger.cpp diagnosticlogger.h
        mibdownloadtransport.cpp mibdownloadtransport.h
        mibcandidatefilter.cpp mibcandidatefilter.h
        miblibrary.cpp miblibrary.h mibcollection.cpp mibcollection.h mibprofile.cpp mibprofile.h
        mibdependencyindex.cpp mibdependencyindex.h mibeffectiveplan.cpp mibeffectiveplan.h
        miblibrarywidget.cpp miblibrarywidget.h mibinventorywidget_test.cpp)
    set_target_properties(snmpb_mib_inventory_widget_test PROPERTIES AUTOMOC ON)
    target_compile_definitions(snmpb_mib_inventory_widget_test PRIVATE
        SNMPB_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    target_link_libraries(snmpb_mib_inventory_widget_test PRIVATE
        Qt6::Core Qt6::Widgets Qt6::Network)
    if(WIN32)
        add_test(NAME snmpb.mib-inventory-widget COMMAND ${CMAKE_COMMAND} -E env
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_mib_inventory_widget_test>)
    else()
        add_test(NAME snmpb.mib-inventory-widget COMMAND ${CMAKE_COMMAND} -E env
            "QT_QPA_PLATFORM=offscreen" $<TARGET_FILE:snmpb_mib_inventory_widget_test>)
    endif()

    add_executable(snmpb_trap_pipeline_test
        mibengine.cpp mibengine.h
        trapdecoder.cpp trapdecoder.h
        traphistorystore.cpp traphistorystore.h traprecord.h
        trappresenter.cpp trappresenter.h
        mibenvironment.cpp mibenvironment.h mibenvironmentextractor.cpp mibenvironmentextractor.h
        mibenvironmentregistry.cpp mibenvironmentregistry.h
        trapservice.cpp trapservice.h
        trap_pipeline_test.cpp)
    set_target_properties(snmpb_trap_pipeline_test PROPERTIES AUTOMOC ON)
    target_link_libraries(snmpb_trap_pipeline_test PRIVATE
        Qt6::Core snmpb_libsmi snmpb_snmp_pp snmpb_libtomcrypt)
    target_compile_definitions(snmpb_trap_pipeline_test PRIVATE
        SNMPB_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
    if(WIN32)
        add_test(NAME snmpb.trap-pipeline COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_trap_pipeline_test>)
    else()
        add_test(NAME snmpb.trap-pipeline COMMAND $<TARGET_FILE:snmpb_trap_pipeline_test>)
    endif()

    add_executable(snmpb_snmp_operations_test
        snmprequestconfig.cpp snmprequestconfig.h
        snmprequestconfigadapter.cpp snmprequestconfigadapter.h
        snmprequestcontext.cpp snmprequestcontext.h
        snmpinstanceoperation.cpp snmpinstanceoperation.h
        snmpinstanceasyncrunner.cpp snmpinstanceasyncrunner.h
        snmptransport.cpp snmptransport.h
        snmptableoperation.cpp snmptableoperation.h
        snmptableasyncrunner.cpp snmptableasyncrunner.h
        tabletraversal.cpp tabletraversal.h
        tableindex.cpp tableindex.h mibenvironment.cpp mibenvironment.h
        snmpreport.cpp snmpreport.h
        fakesnmpagent.h
        snmpoperations_test.cpp)
    set_target_properties(snmpb_snmp_operations_test PROPERTIES AUTOMOC ON)
    target_link_libraries(snmpb_snmp_operations_test PRIVATE
        Qt6::Core snmpb_libsmi snmpb_snmp_pp snmpb_libtomcrypt)
    if(WIN32)
        add_test(NAME snmpb.snmp-operations COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_snmp_operations_test>)
    else()
        add_test(NAME snmpb.snmp-operations COMMAND
            $<TARGET_FILE:snmpb_snmp_operations_test>)
    endif()

    add_executable(snmpb_snmp_table_test
        snmprequestconfig.cpp snmprequestconfig.h
        snmprequestconfigadapter.cpp snmprequestconfigadapter.h
        snmprequestcontext.cpp snmprequestcontext.h
        snmptransport.cpp snmptransport.h
        snmptableoperation.cpp snmptableoperation.h
        tabletraversal.cpp tabletraversal.h
        tableindex.cpp tableindex.h snmpreport.cpp snmpreport.h
        mibenvironment.cpp mibenvironment.h
        fakesnmpagent.h
        snmptable_test.cpp)
    target_link_libraries(snmpb_snmp_table_test PRIVATE
        Qt6::Core snmpb_libsmi snmpb_snmp_pp snmpb_libtomcrypt)
    if(WIN32)
        add_test(NAME snmpb.snmp-table COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_snmp_table_test>)
    else()
        add_test(NAME snmpb.snmp-table COMMAND $<TARGET_FILE:snmpb_snmp_table_test>)
    endif()

    add_executable(snmpb_snmp_walk_test
        snmprequestconfig.cpp snmprequestconfig.h
        snmprequestconfigadapter.cpp snmprequestconfigadapter.h
        snmprequestcontext.cpp snmprequestcontext.h
        snmptransport.cpp snmptransport.h
        snmptableoperation.cpp snmptableoperation.h
        snmpwalkoperation.cpp snmpwalkoperation.h
        snmpwalkasyncrunner.cpp snmpwalkasyncrunner.h
        tabletraversal.cpp tabletraversal.h
        tableindex.cpp tableindex.h snmpreport.cpp snmpreport.h
        mibenvironment.cpp mibenvironment.h
        fakesnmpagent.h
        snmpwalk_test.cpp)
    set_target_properties(snmpb_snmp_walk_test PROPERTIES AUTOMOC ON)
    target_link_libraries(snmpb_snmp_walk_test PRIVATE
        Qt6::Core snmpb_libsmi snmpb_snmp_pp snmpb_libtomcrypt)
    if(WIN32)
        add_test(NAME snmpb.snmp-walk COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_snmp_walk_test>)
    else()
        add_test(NAME snmpb.snmp-walk COMMAND $<TARGET_FILE:snmpb_snmp_walk_test>)
    endif()

    add_executable(snmpb_snmp_single_test
        snmprequestconfig.cpp snmprequestconfig.h
        snmprequestconfigadapter.cpp snmprequestconfigadapter.h
        snmprequestcontext.cpp snmprequestcontext.h
        snmptransport.cpp snmptransport.h
        snmptableoperation.cpp snmptableoperation.h
        snmpwalkoperation.cpp snmpwalkoperation.h
        snmpsingleoperation.cpp snmpsingleoperation.h
        snmpsingleasyncrunner.cpp snmpsingleasyncrunner.h
        snmpquerytext.cpp snmpquerytext.h
        mibvaluesemantics.cpp mibvaluesemantics.h
        tabletraversal.cpp tabletraversal.h
        tableindex.cpp tableindex.h snmpreport.cpp snmpreport.h
        mibenvironment.cpp mibenvironment.h
        fakesnmpagent.h
        snmpsingle_test.cpp)
    set_target_properties(snmpb_snmp_single_test PROPERTIES AUTOMOC ON)
    target_link_libraries(snmpb_snmp_single_test PRIVATE
        Qt6::Core snmpb_libsmi snmpb_snmp_pp snmpb_libtomcrypt)
    if(WIN32)
        add_test(NAME snmpb.snmp-single COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_snmp_single_test>)
    else()
        add_test(NAME snmpb.snmp-single COMMAND $<TARGET_FILE:snmpb_snmp_single_test>)
    endif()

    add_executable(snmpb_query_results_test queryresultspanel.cpp queryresultspanel.h queryresultspanel_test.cpp)
    set_target_properties(snmpb_query_results_test PROPERTIES AUTOMOC ON)
    target_link_libraries(snmpb_query_results_test PRIVATE Qt6::Widgets)
    add_test(NAME snmpb.query-results COMMAND ${CMAKE_COMMAND} -E env
        "QT_QPA_PLATFORM=offscreen" $<TARGET_FILE:snmpb_query_results_test>)
    set_tests_properties(snmpb.query-results PROPERTIES TIMEOUT 30)

    add_executable(snmpb_credentials_dialog_test
        credentialsdialog.cpp credentialsdialog.h
        communitycredentialservice.cpp communitycredentialservice.h communitycredentialrepository.cpp
        usmcredentialservice.cpp usmcredentialservice.h usmcredentialrepository.cpp
        credentialsdialog_test.cpp)
    set_target_properties(snmpb_credentials_dialog_test PROPERTIES AUTOMOC ON)
    target_link_libraries(snmpb_credentials_dialog_test PRIVATE Qt6::Widgets snmpb_snmp_pp snmpb_libtomcrypt)
    add_test(NAME snmpb.credentials-dialog COMMAND ${CMAKE_COMMAND} -E env
        "QT_QPA_PLATFORM=offscreen" $<TARGET_FILE:snmpb_credentials_dialog_test>)

    add_executable(snmpb_snmpv3_credentials_test
        snmprequestconfig.cpp snmprequestconfigadapter.cpp snmptransport.cpp snmpreport.cpp
        snmpv3credentials_test.cpp)
    target_link_libraries(snmpb_snmpv3_credentials_test PRIVATE Qt6::Core Qt6::Network snmpb_snmp_pp snmpb_libtomcrypt)
    if(WIN32)
        add_test(NAME snmpb.snmpv3-credentials COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}" $<TARGET_FILE:snmpb_snmpv3_credentials_test>)
    else()
        add_test(NAME snmpb.snmpv3-credentials COMMAND $<TARGET_FILE:snmpb_snmpv3_credentials_test>)
    endif()
    set_tests_properties(snmpb.snmpv3-credentials PROPERTIES TIMEOUT 30)

    add_executable(snmpb_community_credential_test
        communitycredentialrepository.cpp communitycredentialrepository.h
        communitycredentialservice.cpp communitycredentialservice.h
        credentialrecords.cpp credentialrecords.h
        snmprequestconfig.cpp snmprequestconfig.h
        snmprequestcontext.cpp snmprequestcontext.h
        communitycredential_test.cpp)
    set_target_properties(snmpb_community_credential_test PROPERTIES AUTOMOC ON)
    target_link_libraries(snmpb_community_credential_test PRIVATE Qt6::Core)
    if(WIN32)
        add_test(NAME snmpb.community-credential COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_community_credential_test>)
    else()
        add_test(NAME snmpb.community-credential COMMAND
            $<TARGET_FILE:snmpb_community_credential_test>)
    endif()

    add_executable(snmpb_credential_architecture_test
         snmprequestsettings.h
        credentialrecords.cpp credentialrecords.h
        snmprequestconfig.cpp snmprequestconfig.h
        usmcredentialrepository.cpp usmcredentialrepository.h
        usmcredentialcoordinator.cpp usmcredentialcoordinator.h
        usmcredentialservice.cpp usmcredentialservice.h
        credentialarchitecture_test.cpp)
    set_target_properties(snmpb_credential_architecture_test PROPERTIES AUTOMOC ON)
    target_link_libraries(snmpb_credential_architecture_test PRIVATE Qt6::Core)
    if(WIN32)
        add_test(NAME snmpb.credential-architecture COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_credential_architecture_test>)
    else()
        add_test(NAME snmpb.credential-architecture COMMAND
            $<TARGET_FILE:snmpb_credential_architecture_test>)
    endif()

    add_executable(snmpb_connection_model_test
        agentrequestselection.cpp agentrequestselection.h
         snmprequestsettings.h
        communitycredentialrepository.cpp communitycredentialrepository.h
        communitycredentialservice.cpp communitycredentialservice.h
        connectionmodel.cpp connectionmodel.h
        credentialrecords.cpp credentialrecords.h
        preferencesettings.cpp preferencesettings.h
        snmprequestconfig.cpp snmprequestconfig.h
        usmcredentialrepository.cpp usmcredentialrepository.h
        usmcredentialservice.cpp usmcredentialservice.h
        connectionmodel_test.cpp)
    set_target_properties(snmpb_connection_model_test PROPERTIES AUTOMOC ON)
    target_link_libraries(snmpb_connection_model_test PRIVATE Qt6::Core)
    if(WIN32)
        add_test(NAME snmpb.connection-model COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_connection_model_test>)
    else()
        add_test(NAME snmpb.connection-model COMMAND
            $<TARGET_FILE:snmpb_connection_model_test>)
    endif()

    add_executable(snmpb_workbench_shell_test
        mainwindowlayout.cpp
        mainwindowlayout.h
        startupcoordinator.cpp
        startupcoordinator.h
        workbenchshell.cpp
        workbenchshell.h
        workbenchshell_test.cpp
    )
    set_target_properties(snmpb_workbench_shell_test PROPERTIES AUTOMOC ON)
    target_link_libraries(snmpb_workbench_shell_test PRIVATE Qt6::Core Qt6::Widgets)
    if(WIN32)
        add_test(NAME snmpb.workbench-shell COMMAND ${CMAKE_COMMAND} -E env
            "QT_QPA_PLATFORM=offscreen"
            "QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_workbench_shell_test>)
    else()
        add_test(NAME snmpb.workbench-shell COMMAND ${CMAKE_COMMAND} -E env
            "QT_QPA_PLATFORM=offscreen"
            $<TARGET_FILE:snmpb_workbench_shell_test>)
    endif()

    add_executable(snmpb_table_traversal_test
        tabletraversal.cpp
        tabletraversal.h
        tabletraversal_test.cpp
    )
    target_link_libraries(snmpb_table_traversal_test
        PRIVATE Qt6::Core snmpb_libsmi snmpb_snmp_pp snmpb_libtomcrypt)
    if(WIN32)
        add_test(NAME snmpb.table-traversal COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_table_traversal_test>)
    else()
        add_test(NAME snmpb.table-traversal
            COMMAND $<TARGET_FILE:snmpb_table_traversal_test>)
    endif()

    add_executable(snmpb_table_node_validation_test
        tablenodevalidation.cpp
        tablenodevalidation.h
        tablenodevalidation_test.cpp
    )
    target_link_libraries(snmpb_table_node_validation_test PRIVATE Qt6::Core)
    add_test(NAME snmpb.table-node-validation
        COMMAND $<TARGET_FILE:snmpb_table_node_validation_test>)

    add_executable(snmpb_synchronous_config_test
        agentrequestselection.cpp
        agentrequestselection.h
        snmprequestconfig.cpp
        snmprequestconfig.h
        snmprequestconfigadapter.cpp
        snmprequestconfigadapter.h
        synchronousrequestconfig_test.cpp
    )
    target_link_libraries(snmpb_synchronous_config_test
        PRIVATE Qt6::Core snmpb_snmp_pp snmpb_libtomcrypt)

    if(WIN32)
        add_test(NAME snmpb.synchronous-config COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_synchronous_config_test>)
    else()
        add_test(NAME snmpb.synchronous-config
            COMMAND $<TARGET_FILE:snmpb_synchronous_config_test>)
    endif()

    add_executable(snmpb_agent_selection_test
        agentrequestselection.cpp
        agentrequestselection.h
        snmprequestconfig.cpp
        snmprequestconfig.h
        agentrequestselection_test.cpp
    )
    target_link_libraries(snmpb_agent_selection_test PRIVATE Qt6::Core)

    if(WIN32)
        add_test(NAME snmpb.agent-selection COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_agent_selection_test>)
    else()
        add_test(NAME snmpb.agent-selection
            COMMAND $<TARGET_FILE:snmpb_agent_selection_test>)
    endif()

    add_executable(snmpb_request_context_test
        mibenvironment.cpp
        mibenvironment.h
        mibenvironmentregistry.cpp
        mibenvironmentregistry.h
        snmprequestconfig.cpp
        snmprequestconfig.h
        snmprequestcontext.cpp
        snmprequestcontext.h
        snmprequestcontext_test.cpp
    )
    target_link_libraries(snmpb_request_context_test PRIVATE Qt6::Core)

    if(WIN32)
        add_test(NAME snmpb.request-context COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_request_context_test>)
    else()
        add_test(NAME snmpb.request-context
            COMMAND $<TARGET_FILE:snmpb_request_context_test>)
    endif()

    add_executable(snmpb_request_config_test
        snmprequestconfig.cpp
        snmprequestconfig.h
        snmprequestconfigadapter.cpp
        snmprequestconfigadapter.h
        snmprequestconfig_test.cpp
    )
    target_link_libraries(snmpb_request_config_test
        PRIVATE Qt6::Core snmpb_snmp_pp snmpb_libtomcrypt)

    if(WIN32)
        add_test(NAME snmpb.request-config COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_request_config_test>)
    else()
        add_test(NAME snmpb.request-config
            COMMAND $<TARGET_FILE:snmpb_request_config_test>)
    endif()

    add_executable(snmpb_preferences_settings_test
        preferencesettings.cpp
        preferencesettings.h
        preferencesettings_test.cpp
    )
    target_link_libraries(snmpb_preferences_settings_test PRIVATE Qt6::Core)

    if(WIN32)
        add_test(NAME snmpb.preferences-settings COMMAND ${CMAKE_COMMAND} -E env
            "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
            $<TARGET_FILE:snmpb_preferences_settings_test>)
    else()
        add_test(NAME snmpb.preferences-settings
            COMMAND $<TARGET_FILE:snmpb_preferences_settings_test>)
    endif()

    add_test(
        NAME snmpb.launch-smoke
        COMMAND ${CMAKE_COMMAND}
            -DSNMPB_EXECUTABLE=$<TARGET_FILE:snmpb>
            -DSNMPB_QT_BIN_DIR=$<TARGET_FILE_DIR:Qt6::Core>
            -DSNMPB_RUNTIME_DATA_DIR=$<TARGET_FILE_DIR:snmpb>
            -DSNMPB_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DSNMPB_SMOKE_DIR=${CMAKE_CURRENT_BINARY_DIR}/launch-smoke-config
            -P ${PROJECT_SOURCE_DIR}/cmake/RunSnmpbLaunchSmoke.cmake
    )
    # The script performs two isolated launches, each with its own 45-second
    # timeout. Leave enough outer margin for both launches and CMake setup.
    set_tests_properties(snmpb.launch-smoke PROPERTIES TIMEOUT 120)

    # Starts the real application headless on libraries with every kind of MIB problem and
    # checks that it still loads what it can and reports what it could not, and why.
    add_test(
        NAME snmpb.tolerance-scenarios
        COMMAND ${CMAKE_COMMAND}
            -DSNMPB_EXECUTABLE=$<TARGET_FILE:snmpb>
            -DSNMPB_QT_BIN_DIR=$<TARGET_FILE_DIR:Qt6::Core>
            -DSNMPB_SOURCE_DIR=${PROJECT_SOURCE_DIR}
            -DSNMPB_TOLERANCE_DIR=${CMAKE_CURRENT_BINARY_DIR}/tolerance-scenarios
            -DSNMPB_PYTHON=${Python3_EXECUTABLE}
            -P ${PROJECT_SOURCE_DIR}/cmake/RunSnmpbToleranceScenarios.cmake
    )
    set_tests_properties(snmpb.tolerance-scenarios PROPERTIES TIMEOUT 1200)

    foreach(close_state IN ITEMS CatalogLoading ProfilePreparing EnvironmentBuilding)
        # Each run starts from empty state so it exercises a true first launch.
        # Without this the state directory persists between runs and keeps
        # whatever profiles an older build created.
        add_test(NAME snmpb.startup-close-${close_state}-reset
            COMMAND ${CMAKE_COMMAND} -E remove_directory
                ${CMAKE_CURRENT_BINARY_DIR}/startup-close-${close_state})
        set_tests_properties(snmpb.startup-close-${close_state}-reset PROPERTIES
            FIXTURES_SETUP startup-close-${close_state}-state)
        add_test(NAME snmpb.startup-close-${close_state}
            COMMAND ${CMAKE_COMMAND} -E env
                "QT_QPA_PLATFORM=offscreen"
                "PATH=$<TARGET_FILE_DIR:Qt6::Core>;$ENV{PATH}"
                $<TARGET_FILE:snmpb>
                --launch-smoke-test ${CMAKE_CURRENT_BINARY_DIR}/startup-close-${close_state}
                --startup-close-state ${close_state})
        set_tests_properties(snmpb.startup-close-${close_state} PROPERTIES TIMEOUT 60
            FIXTURES_REQUIRED startup-close-${close_state}-state)
    endforeach()

    add_executable(snmpb_appearance_test appearance.cpp appearance.h appearancecolors.h themedtextedit.h
        appearance_test.cpp mibhighlighter.cpp mibhighlighter.h appearance.qrc)
    set_target_properties(snmpb_appearance_test PROPERTIES AUTOMOC ON AUTORCC ON)
    target_link_libraries(snmpb_appearance_test PRIVATE Qt6::Widgets Qt6::Svg)
    add_test(NAME snmpb.appearance COMMAND ${CMAKE_COMMAND} -E env
        "QT_QPA_PLATFORM=offscreen" $<TARGET_FILE:snmpb_appearance_test>)
    set_tests_properties(snmpb.appearance PROPERTIES TIMEOUT 30)

    snmpb_configure_qt_test_runtime(
        snmpb.appearance
        snmpb.query-results
        snmpb.credentials-dialog
        snmpb.snmpv3-credentials
        snmpb.mib-engine
        snmpb.mib-environment-manager
        snmpb.mib-dependency-index
        snmpb.mib-effective-plan
        snmpb.mib-startup-path
        snmpb.stability-diagnostics
        snmpb.product-identity
        snmpb.mib-service
        snmpb.mib-environment
        snmpb.mib-environment-large
        snmpb.mib-environment-consumers
        snmpb.mib-environment-transitions
        snmpb.mib-environment-consumers-large
        snmpb.mib-library
        snmpb.mib-profile
        snmpb.mib-inventory-widget
        snmpb.trap-pipeline
        snmpb.snmp-operations
        snmpb.snmp-table
        snmpb.community-credential
        snmpb.credential-architecture
        snmpb.table-traversal
        snmpb.table-node-validation
        snmpb.synchronous-config
        snmpb.agent-selection
        snmpb.request-context
        snmpb.request-config
        snmpb.preferences-settings
    )
endif()
