# SXE executable: reads EngineConfig.ini and starts the engine
add_executable(SXE
    main.cpp
)

target_link_libraries(SXE PRIVATE
    Engine
)

install(TARGETS SXE RUNTIME DESTINATION ${SXE_BIN_DIR})
