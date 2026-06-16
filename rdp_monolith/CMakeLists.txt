cmake_minimum_required(VERSION 3.15)
project(RDPWrapperService CXX RC) # Фикс: Добавлена явная поддержка компилятора ресурсов RC

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG -flto")

# Фикс: Добавляем resource.rc в список исходников
set(PROJECT_SOURCES
    main.cpp
    service_engine.hpp
    resource.rc
)

add_executable(rdpwrap_srv ${PROJECT_SOURCES})

target_link_options(rdpwrap_srv PRIVATE 
    "-static"
    "-static-libgcc"
    "-static-libstdc++"
)

target_link_libraries(rdpwrap_srv PRIVATE
    ws2_32 wininet bcrypt iphlpapi wtsapi32 ole32 oleaut32 wbemuuid shell32
)

set_target_properties(rdpwrap_srv PROPERTIES OUTPUT_NAME "rdpwrap_srv")
