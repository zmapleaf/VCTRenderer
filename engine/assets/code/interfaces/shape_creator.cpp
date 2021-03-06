#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include "shape_creator.h"

#include "main_menu.h"
#include "../../../scene/scene.h"
#include "../../../scene/material.h"
#include <glm/gtc/type_ptr.hpp>
#include "../../../rendering/primitives/shapes.h"
#include "../renderers/voxelizer_renderer.h"
#include "../../../core/assets_manager.h"
#include "../renderers/shadow_map_renderer.h"
#include "../../../scene/light.h"

using namespace ImGui;

bool ShapeName(void * data, int idx, const char ** out_text)
{
    auto items = static_cast<std::vector<std::string> *>(data);

    if (out_text)
    {
        auto begin = items->begin();
        advance(begin, idx);
        *out_text = begin->c_str();
    }

    return true;
}

void UIShapeCreator::Draw()
{
    if (!UIMainMenu::drawSceneNodes) { return; }

    static auto &assets = AssetsManager::Instance();
    static auto &voxel = *static_cast<VoxelizerRenderer *>
                         (assets->renderers["Voxelizer"].get());
    static auto scene = static_cast<Scene *>(nullptr);
    static auto node = static_cast<Node *>(nullptr);
    static auto material = static_cast<Material *>(nullptr);
    // control variables
    static auto selected = -1;
    static glm::vec3 position;
    static glm::vec3 rotation;
    static glm::vec3 scale;
    static glm::vec3 ambient;
    static glm::vec3 specular;
    static glm::vec3 diffuse;
    static glm::vec3 emissive;
    static float shininess;
    static std::map<Scene *, std::vector<Node *>> addedNodes;
    static std::map<Node *, bool> customMat;
    static auto shapesNames = Shapes::ShapeNameList();
    static int shapeSelected = -1;
    static std::vector<char> name;

    // active scene changed
    if (scene != Scene::Active().get())
    {
        scene = Scene::Active().get();
        selected = -1;
        node = nullptr;
        auto it = addedNodes.find(scene);

        if(it == addedNodes.end())
        {
            addedNodes[scene] = std::vector<Node *>();
        }
    }

    // no active scene
    if (!scene) { return; }

    // begin editor
    Begin("Objects", &UIMainMenu::drawSceneNodes);
    PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
    Columns(2);
    Combo("", &shapeSelected, ShapeName, &shapesNames,
          static_cast<int>(shapesNames.size()));
    SameLine();

    if (shapeSelected >= 0 && Button("Create Shape"))
    {
        auto shape = Shapes::GetShape(shapesNames[shapeSelected]);
        scene->rootNode->nodes.push_back(shape);
        scene->rootNode->BuildDrawList();
        addedNodes[scene].push_back(shape.get());
        // add material from shape creator to scene
        auto it = find(scene->materials.begin(), scene->materials.end(),
                       shape->meshes[0]->material);

        if(it == scene->materials.end())
        {
            scene->materials.push_back(shape->meshes[0]->material);
        }

        voxel.RevoxelizeScene();
    }

    for (auto i = 0; i < addedNodes[scene].size(); i++)
    {
        auto &current = addedNodes[scene][i];
        PushID(i);
        BeginGroup();

        // selected becomes the clicked selectable
        if (Selectable(current->name.c_str(), i == selected))
        {
            selected = i;
            node = current;
            position = node->Position();
            rotation = degrees(node->Angles());
            scale = node->Scale();
            // all shapes have only mesh, thus one material
            material = node->meshes[0]->material.get();
            ambient = material->Ambient();
            diffuse = material->Diffuse();
            specular = material->Specular();
            shininess = material->Shininess();
            emissive = material->Emissive();
            // copy name to a standard vector
            name.clear();
            copy(node->name.begin(), node->name.end(), back_inserter(name));
            name.push_back('\0');
        }

        EndGroup();
        PopID();
    }

    NextColumn();

    if (selected >= 0 && node != nullptr)
    {
        if (InputText("Name", name.data(), name.size()))
        {
            node->name = std::string(name.data());
        }

        if (DragFloat3("Position", value_ptr(position), 0.1f))
        {
            node->Position(position);
        }

        if (DragFloat3("Rotation", value_ptr(rotation), 0.1f))
        {
            node->Rotation(radians(rotation));
        }

        if (DragFloat3("Scale", value_ptr(scale), 0.1f))
        {
            node->Scale(scale);
        }

        auto it = customMat.find(node);

        if((it == customMat.end() || !it->second) &&
                Button("Create Custom Material"))
        {
            auto newMesh = std::make_shared<MeshDrawer>(*node->meshes[0]);
            auto newMaterial = std::make_shared<Material>(*node->meshes[0]->material);
            newMaterial->name = node->name + "CustomMat";
            material = newMaterial.get();
            ambient = material->Ambient();
            diffuse = material->Diffuse();
            specular = material->Specular();
            shininess = material->Shininess();
            scene->materials.push_back(newMaterial);
            node->meshes[0] = move(newMesh);
            node->meshes[0]->material = move(newMaterial);
            customMat[node] = true;
        }

        // draw custom material values
        if (ColorEdit3("Ambient", value_ptr(ambient)))
        {
            material->Ambient(ambient);
        }

        if (ColorEdit3("Diffuse", value_ptr(diffuse)))
        {
            material->Diffuse(diffuse);
            voxel.RevoxelizeScene();
        }

        if (ColorEdit3("Specular", value_ptr(specular)))
        {
            material->Specular(specular);
        }

        if (DragFloat("Glossiness", &shininess, 0.001f, 0.0f, 1.0f))
        {
            material->Shininess(shininess);
        }

        if (ColorEdit3("Emissive", value_ptr(emissive)))
        {
            material->Emissive(emissive);
            voxel.RevoxelizeScene();
        }

        if (Button("Delete Shape"))
        {
            static auto &shadowRender = *static_cast<ShadowMapRenderer *>
                                        (assets->renderers["Shadowmapping"].get());
            auto itScene = find_if(scene->rootNode->nodes.begin(),
                                   scene->rootNode->nodes.end(),
                                   [ = ](const std::shared_ptr<Node> &ptr)
            {
                return ptr.get() == node;
            });

            // shape had custom material
            if (it != customMat.end() && it->second)
            {
                auto itMaterial = find(scene->materials.begin(),
                                       scene->materials.end(),
                                       node->meshes[0]->material);

                if (itMaterial != scene->materials.end())
                {
                    scene->materials.erase(itMaterial);
                }
            }

            // remove from scene root node
            if(itScene != scene->rootNode->nodes.end())
            {
                scene->rootNode->nodes.erase(itScene);
                scene->rootNode->BuildDrawList();
            }

            auto itMap = find(addedNodes[scene].begin(),
                              addedNodes[scene].end(), node);

            // remove from scene-node ui map
            if (itMap != addedNodes[scene].end())
            {
                addedNodes[scene].erase(itMap);
            }

            voxel.RevoxelizeScene();
            selected = -1;
            node = nullptr;

            if (shadowRender.Caster()) shadowRender.Caster()->RegisterChange();
        }
    }
    else
    {
        Text("No Node Selected");
    }

    PopStyleVar();
    End();
}
