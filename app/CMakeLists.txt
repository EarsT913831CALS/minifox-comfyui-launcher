add_subdirectory(frontend/Minifox/App)

if(MINIFOX_BUILD_TESTS)
    add_subdirectory(backend/version/tests)
endif()
