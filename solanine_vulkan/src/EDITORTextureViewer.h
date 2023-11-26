#pragma once
#ifdef _DEVELOP

#include "Entity.h"
struct RenderObject;
class RenderObjectManager;
struct EDITORTextureViewer_XData;


class EDITORTextureViewer : public Entity
{
public:
    static const std::string TYPE_NAME;

    EDITORTextureViewer(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds);
    ~EDITORTextureViewer();

    void dump(DataSerializer& ds);
    void load(DataSerialized& ds);
    std::string getTypeName() { return TYPE_NAME; };

    void renderImGui();

private:
    EDITORTextureViewer_XData* d;
};

#endif
