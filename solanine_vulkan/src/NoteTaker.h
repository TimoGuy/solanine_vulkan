#pragma once

#include "Entity.h"
#include "Imports.h"
struct RenderObject;
class RenderObjectManager;


class NoteTaker : public Entity
{
public:
    static const std::string TYPE_NAME;

    NoteTaker(EntityManager* em, RenderObjectManager* rom, DataSerialized* ds);
    ~NoteTaker();

    void dump(DataSerializer& ds);
    void load(DataSerialized& ds);
    std::string getTypeName() { return TYPE_NAME; };

    void renderImGui();

private:
    RenderObject*        _renderObj;
    RenderObjectManager* _rom;

    // Load Props
    mat4 _load_transform = GLM_MAT4_IDENTITY_INIT;

    // Tweak Props
    std::string          _notes;
};
