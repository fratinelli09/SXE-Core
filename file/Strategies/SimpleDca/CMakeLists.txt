# example DCA strategy (case study, docs/case_study.md) -> Target/Bin/Strategies/libSimpleDca.so
# (Windows: libSimpleDca.dll); its configuration is Target/Config/SimpleDca.ini
sxe_add_strategy(NAME    SimpleDca
                 SOURCES CSimpleDcaStrategy.cpp)
