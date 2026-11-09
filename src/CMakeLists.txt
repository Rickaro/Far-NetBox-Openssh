#===============================================================================
# NetBox Main Plugin CMakeLists.tx
#
# Purpose: Build the NetBox plugin DLL for Far Manager
#
# Components:
#   - Base library (src/base/, src/nbcore/)
#   - FileZilla FTP implementation (src/filezilla/)
#   - WinSCP core (src/core/, src/windows/)
#   - NetBox plugin (src/NetBox/)
#   - Resources (src/resource/)
#===============================================================================

#===============================================================================
# CMake Includes
#===============================================================================

include(${CMAKE_SOURCE_DIR}/cmake/ucm.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/NetBox.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/Install.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/SourceGroups.cmake)
include(${CMAKE_SOURCE_DIR}/cmake/TargetConfiguration.cmake)

#===============================================================================
# Source Configuration
#===============================================================================

# Configure source groups based on build type
netbox_configure_source_groups()

#===============================================================================
# Resources Targe
#===============================================================================

add_custom_target(NetBoxResources SOURCES
  NetBox/NetBoxEng.lng
  NetBox/NetBoxFr.lng
  NetBox/NetBoxRus.lng
  NetBox/NetBoxPol.lng
  NetBox/NetBoxSpa.lng
  NetBox/resource.h.template
  NetBox/NetBox.rc
  NetBox/NetBox.rc.template
  resource/rtlconsts.rc
  resource/TextsCore1.rc
  resource/TextsFileZilla.rc
  resource/TextsWin1.rc
  ${CMAKE_SOURCE_DIR}/cmake/copy_file.cmake
  ${CMAKE_SOURCE_DIR}/.github/workflows/release.yml
  ${CMAKE_SOURCE_DIR}/ChangeLog
  ${CMAKE_SOURCE_DIR}/README.md
)

#===============================================================================
# NetBox Shared Library (DLL)
#===============================================================================

# Create the NetBox targe
add_library(NetBox SHARED ${NETBOX_SOURCES})

# Configure the target with includes, libraries, and compiler options
netbox_configure_target(NetBox)

# Custom build rule for vc_crt_fix.asm (MASM) on MSVC x64/x86
if(MSVC AND NOT DEFINED CMAKE_ULINK)
  set(VC_CRT_FIX_ASM "${CMAKE_CURRENT_SOURCE_DIR}/NetBox/vc_crt_fix.asm")
  if(PROJECT_PLATFORM STREQUAL "x64")
    set(ASM_COMPILER ml64.exe)
    set(ASM_FLAGS /c /Cx /DX64)
  elseif(PROJECT_PLATFORM STREQUAL "x86")
    set(ASM_COMPILER ml.exe)
    set(ASM_FLAGS /c /Cx /coff)
  endif()
  if(DEFINED ASM_COMPILER)
    get_filename_component(MSVC_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(ASM_EXE "${MSVC_BIN_DIR}/${ASM_COMPILER}")
    set(ASM_OBJ "${CMAKE_CURRENT_BINARY_DIR}/vc_crt_fix.obj")
    add_custom_command(
      OUTPUT ${ASM_OBJ}
      COMMAND "${ASM_EXE}" ${ASM_FLAGS} "${VC_CRT_FIX_ASM}"
      DEPENDS ${VC_CRT_FIX_ASM}
      COMMENT "Assembling vc_crt_fix.asm with ${ASM_COMPILER}"
    )
    set_source_files_properties(${ASM_OBJ} PROPERTIES GENERATED TRUE)
    target_sources(NetBox PRIVATE ${ASM_OBJ})
  endif()
endif()

#===============================================================================
# Plugin Directory Installation (Optional)
#===============================================================================

netbox_setup_plugin_installation(NetBox)

#===============================================================================
# NetBoxAskpass helper for the external OpenSSH backend
#===============================================================================

add_executable(NetBoxAskpass NetBoxAskpass/NetBoxAskpass.cpp)
netbox_apply_compile_options(NetBoxAskpass)
target_link_libraries(NetBoxAskpass PRIVATE user32.lib gdi32.lib kernel32.lib)
set_target_properties(NetBoxAskpass PROPERTIES
  LINK_FLAGS "/SUBSYSTEM:CONSOLE /ENTRY:wmainCRTStartup"
  OUTPUT_NAME "NetBoxAskpass"
)

if(OPT_CREATE_PLUGIN_DIR)
  set(NB_PLUGIN_OUT_DIR "${CMAKE_SOURCE_DIR}/Far3_${PROJECT_PLATFORM}/Plugins/NetBox")
  add_custom_command(TARGET NetBoxAskpass POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${NB_PLUGIN_OUT_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:NetBoxAskpass>"
      "${NB_PLUGIN_OUT_DIR}/NetBoxAskpass.exe"
    COMMENT "Copying NetBoxAskpass.exe to plugin directory"
  )
endif()

#===============================================================================
# tinylog Unit Tests
#===============================================================================
if(OPT_CREATE_TESTS)
  if(EXISTS ${CMAKE_SOURCE_DIR}/libs/tinylog/tests/test_tinylog.cpp)
    add_executable(test_tinylog
      ${CMAKE_SOURCE_DIR}/libs/tinylog/tests/test_tinylog.cpp
      ${CMAKE_SOURCE_DIR}/src/nbcore/nbmemory.cpp
      ${CMAKE_SOURCE_DIR}/src/nbcore/nbstring.cpp
      ${CMAKE_SOURCE_DIR}/libs/tinylog/src/TinyLog.cpp
      ${CMAKE_SOURCE_DIR}/libs/tinylog/src/LogStream.cpp
      ${CMAKE_SOURCE_DIR}/libs/tinylog/src/Buffer.cpp
      ${CMAKE_SOURCE_DIR}/libs/tinylog/src/Utils.cpp
    )
    netbox_configure_target(test_tinylog)
    target_compile_definitions(test_tinylog PRIVATE -DTINYLOG_EXPOR)
    add_test(NAME tinylog COMMAND test_tinylog)
  endif()
  add_executable(test_sessionhistory
    ${CMAKE_SOURCE_DIR}/tests/nbcore/SessionHistoryTest.cpp
    ${CMAKE_SOURCE_DIR}/src/nbcore/SessionHistory.cpp
    ${CMAKE_SOURCE_DIR}/src/nbcore/nbmemory.cpp
    ${CMAKE_SOURCE_DIR}/src/nbcore/nbstring.cpp
    ${CMAKE_SOURCE_DIR}/src/nbcore/nbutils.cpp
    ${CMAKE_SOURCE_DIR}/src/base/UnicodeString.cpp
    ${CMAKE_SOURCE_DIR}/src/base/Classes.cpp
    ${CMAKE_SOURCE_DIR}/src/base/Sysutils.cpp
    ${CMAKE_SOURCE_DIR}/src/base/StrUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/base/WideStrUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/base/Common.cpp
  )
  netbox_configure_target(test_sessionhistory)
  target_link_options(test_sessionhistory PRIVATE /SUBSYSTEM:CONSOLE)
  add_test(NAME sessionhistory COMMAND test_sessionhistory)
  add_executable(test_secure_string
    ${CMAKE_SOURCE_DIR}/tests/core/test_secure_string.cpp
    ${CMAKE_SOURCE_DIR}/src/base/SecureString.cpp
    ${CMAKE_SOURCE_DIR}/src/base/UnicodeString.cpp
    ${CMAKE_SOURCE_DIR}/src/base/Classes.cpp
    ${CMAKE_SOURCE_DIR}/src/base/Sysutils.cpp
    ${CMAKE_SOURCE_DIR}/src/base/StrUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/base/WideStrUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/base/Common.cpp
  )
  netbox_configure_target(test_secure_string)
  target_link_options(test_secure_string PRIVATE /SUBSYSTEM:CONSOLE)
  add_test(NAME secure_string COMMAND test_secure_string)

  add_executable(test_named_object
    ${CMAKE_SOURCE_DIR}/tests/core/test_named_object.cpp
    ${CMAKE_SOURCE_DIR}/src/core/NamedObjs.cpp
    ${CMAKE_SOURCE_DIR}/src/base/ObjIDs.cpp
    ${CMAKE_SOURCE_DIR}/src/base/UnicodeString.cpp
    ${CMAKE_SOURCE_DIR}/src/base/Classes.cpp
    ${CMAKE_SOURCE_DIR}/src/base/Sysutils.cpp
    ${CMAKE_SOURCE_DIR}/src/base/StrUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/base/WideStrUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/base/Common.cpp
  )
  netbox_configure_target(test_named_object)
  target_link_options(test_named_object PRIVATE /SUBSYSTEM:CONSOLE)
  add_test(NAME named_object COMMAND test_named_object)

  add_executable(test_master_password
    ${CMAKE_SOURCE_DIR}/tests/integration/test_master_password.cpp
    ${CMAKE_SOURCE_DIR}/src/base/SecureString.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Cryptography.cpp
    ${CMAKE_SOURCE_DIR}/src/base/UnicodeString.cpp
    ${CMAKE_SOURCE_DIR}/src/base/Classes.cpp
    ${CMAKE_SOURCE_DIR}/src/base/Sysutils.cpp
    ${CMAKE_SOURCE_DIR}/src/base/StrUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/base/WideStrUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/base/Common.cpp
  )
  netbox_configure_target(test_master_password)
  target_link_options(test_master_password PRIVATE /SUBSYSTEM:CONSOLE)
  target_link_libraries(test_master_password PRIVATE ssleay32 libeay32)
  add_test(NAME master_password COMMAND test_master_password)
endif()

#===============================================================================
# OpenSSH configuration parser tests
#===============================================================================

if(OPT_CREATE_OPENSSH_TESTS)
  add_executable(test_openssh_config
    ${CMAKE_SOURCE_DIR}/tests/core/test_openssh_config.cpp
    ${CMAKE_SOURCE_DIR}/src/core/OpenSshConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/base/UnicodeString.cpp
    ${CMAKE_SOURCE_DIR}/src/base/Classes.cpp
    ${CMAKE_SOURCE_DIR}/src/base/Sysutils.cpp
    ${CMAKE_SOURCE_DIR}/src/base/StrUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/base/WideStrUtils.cpp
    ${CMAKE_SOURCE_DIR}/src/base/Common.cpp
    ${CMAKE_SOURCE_DIR}/src/nbcore/nbmemory.cpp
    ${CMAKE_SOURCE_DIR}/src/nbcore/nbstring.cpp
    ${CMAKE_SOURCE_DIR}/src/nbcore/nbutils.cpp
  )
  netbox_configure_target(test_openssh_config)
  target_link_options(test_openssh_config PRIVATE /SUBSYSTEM:CONSOLE)
  add_test(NAME openssh_config COMMAND test_openssh_config)
endif()