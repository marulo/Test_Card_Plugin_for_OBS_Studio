cmake_minimum_required(VERSION 3.10)

project(obs-test-card VERSION 0.1.0)

# Opciones de compilación
option(ENABLE_QT "Use Qt functionality" OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 11)

# Nombre del archivo de salida (dll)
set(OUTPUT_NAME "test-source")

if(MSVC)
    # Flags recomendados para MSVC
    add_compile_options(/W4 /wd4100 /wd4996 /wd4201 /wd4458)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    
    # Usar Runtime Estático para evitar error de DLL faltante (VCRUNTIME140.dll etc)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

# Buscar libobs
if(DEFINED LIBOBS_LIB AND DEFINED LIBOBS_INCLUDE_DIR)
    message(STATUS "Using provided libobs paths")
    add_library(OBS::libobs SHARED IMPORTED)
    set_target_properties(OBS::libobs PROPERTIES
        IMPORTED_IMPLIB "${LIBOBS_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBOBS_INCLUDE_DIR}"
    )
    if(DEFINED LIBOBS_CONFIG_DIR)
        set_property(TARGET OBS::libobs APPEND PROPERTY INTERFACE_INCLUDE_DIRECTORIES "${LIBOBS_CONFIG_DIR}")
    endif()
else()
    find_package(libobs REQUIRED)
endif()

# Include directories
include_directories(${LIBOBS_INCLUDE_DIRS})
# Asegurar que se puedan incluir cabeceras locales (como plugin-support.h)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/src)

# Configurar archivo de soporte (versión/nombre)
configure_file(src/plugin-support.c.in src/plugin-support.c)

# Definir la librería del plugin
add_library(${PROJECT_NAME} MODULE
    src/plugin-main.c
    src/test-source.c
    ${CMAKE_CURRENT_BINARY_DIR}/src/plugin-support.c
)

# Linkear con libobs
target_link_libraries(${PROJECT_NAME} PRIVATE OBS::libobs)

# Dependencias opcionales de QT
if(ENABLE_QT)
    find_package(Qt6 COMPONENTS Widgets Core REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
    target_compile_definitions(${PROJECT_NAME} PRIVATE ENABLE_QT)
endif()

# Propiedades del target
set_target_properties(${PROJECT_NAME} PROPERTIES 
    OUTPUT_NAME "${OUTPUT_NAME}"
    PREFIX ""
)

# Copiar recursos (data) al directorio de salida para facilitar pruebas
file(GLOB_RECURSE RESOURCE_FILES "data/*")
file(COPY ${RESOURCE_FILES} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/data")

