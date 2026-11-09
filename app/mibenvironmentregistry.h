#ifndef MIBENVIRONMENTREGISTRY_H
#define MIBENVIRONMENTREGISTRY_H
#include "mibenvironment.h"
class MibEnvironmentRegistry final {
public:
    static void publish(MibEnvironmentPtr environment);
    static bool publishMaterialization(MibEnvironmentPtr environment);
    static bool isUsableMaterialization(const MibEnvironmentPtr &environment);
    static MibEnvironmentPtr active();
};
#endif
