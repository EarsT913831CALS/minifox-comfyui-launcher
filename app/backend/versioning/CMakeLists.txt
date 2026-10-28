qt_add_library(minifox_versioning_backend STATIC
    Dependencies.cpp
    Dependencies.h
    EntryModel.cpp
    EntryModel.h
    Extensions.cpp
    Extensions.h
    Git.cpp
    Git.h
    Repository.cpp
    Repository.h
    Transaction.cpp
    Transaction.h
    VersionService.cpp
    VersionService.h
)

target_include_directories(minifox_versioning_backend
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(minifox_versioning_backend
    PUBLIC
        Qt6::Core
        Qt6::Network
        minifox_platform
        minifox_shared_backend
    PRIVATE
        Qt6::Concurrent
        minifox_application_settings_backend
        minifox_configuration_backend
)
