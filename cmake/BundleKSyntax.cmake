# Builds KSyntaxHighlighting (MIT) and extra-cmake-modules (BSD, build-time only) from source as an
# *isolated* static library inside the build tree, so the highlighter ships inside CrucibleCode itself.
#
#   crucible_build_ksyntax(<out-var>)   ->  <out-var> = install prefix on success, empty on failure
#
# It runs once (the result is cached in <build>/_ksyntax). Any failure only produces a warning: the IDE then
# uses its built-in highlighter, which already covers C, C++, C#, Markdown and many other languages.

function(crucible_build_ksyntax out_var)
    set(${out_var} "" PARENT_SCOPE)

    set(base "${CMAKE_BINARY_DIR}/_ksyntax")
    set(prefix "${base}/install")
    set(logfile "${base}/build.log")
    file(MAKE_DIRECTORY "${base}")

    file(GLOB existing "${prefix}/lib*/cmake/KF6SyntaxHighlighting/KF6SyntaxHighlightingConfig.cmake")
    if(existing)
        set(${out_var} "${prefix}" PARENT_SCOPE)
        return()
    endif()

    find_package(Git QUIET)
    if(NOT GIT_FOUND)
        message(WARNING "KSyntaxHighlighting: git was not found, using the built-in highlighter.")
        return()
    endif()
    if(NOT DEFINED Qt6_DIR)
        message(WARNING "KSyntaxHighlighting: Qt6_DIR is not set.")
        return()
    endif()

    message(STATUS "Bundling KSyntaxHighlighting ${CRUCIBLE_KF_TAG} (first configure only, takes a few minutes)...")
    file(WRITE "${logfile}" "KSyntaxHighlighting bundle log\n")

    # helper: run a command, append its output to the log, fail softly
    macro(_crucible_run description)
        execute_process(COMMAND ${ARGN}
            RESULT_VARIABLE _rc OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
        file(APPEND "${logfile}" "\n=== ${description} (exit ${_rc}) ===\n${_out}\n${_err}\n")
        if(NOT _rc EQUAL 0)
            message(WARNING "KSyntaxHighlighting: step '${description}' failed - using the built-in highlighter. See ${logfile}")
            return()
        endif()
    endmacro()

    foreach(repo IN ITEMS extra-cmake-modules syntax-highlighting)
        if(NOT EXISTS "${base}/src-${repo}/CMakeLists.txt")
            _crucible_run("clone ${repo}"
                "${GIT_EXECUTABLE}" clone --depth 1 --branch "${CRUCIBLE_KF_TAG}"
                "https://invent.kde.org/frameworks/${repo}.git" "${base}/src-${repo}")
        endif()
    endforeach()

    set(common_args -G "${CMAKE_GENERATOR}" -DCMAKE_INSTALL_PREFIX=${prefix} -DBUILD_TESTING=OFF)
    if(CMAKE_MAKE_PROGRAM)
        list(APPEND common_args -DCMAKE_MAKE_PROGRAM=${CMAKE_MAKE_PROGRAM})
    endif()
    if(CMAKE_CXX_COMPILER)
        list(APPEND common_args -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
    endif()
    if(CMAKE_C_COMPILER)
        list(APPEND common_args -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER})
    endif()

    # 1. extra-cmake-modules (just CMake files)
    _crucible_run("configure ECM" "${CMAKE_COMMAND}" -S "${base}/src-extra-cmake-modules" -B "${base}/build-ecm"
        ${common_args} -DBUILD_HTML_DOCS=OFF -DBUILD_MAN_DOCS=OFF -DBUILD_QTHELP_DOCS=OFF)
    _crucible_run("install ECM" "${CMAKE_COMMAND}" --install "${base}/build-ecm")

    # 2. KSyntaxHighlighting as a static library
    _crucible_run("configure KSyntaxHighlighting" "${CMAKE_COMMAND}" -S "${base}/src-syntax-highlighting"
        -B "${base}/build-ksh" ${common_args}
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=${prefix} -DECM_DIR=${prefix}/share/ECM/cmake
        -DQt6_DIR=${Qt6_DIR} -DBUILD_SHARED_LIBS=OFF -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBUILD_QCH=OFF -DKSYNTAXHIGHLIGHTING_USE_GUI=ON)
    _crucible_run("build KSyntaxHighlighting" "${CMAKE_COMMAND}" --build "${base}/build-ksh" --config Release --parallel)
    _crucible_run("install KSyntaxHighlighting" "${CMAKE_COMMAND}" --install "${base}/build-ksh" --config Release)

    set(${out_var} "${prefix}" PARENT_SCOPE)
endfunction()
