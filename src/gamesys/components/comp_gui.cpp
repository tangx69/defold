#include "comp_gui.h"

#include <string.h>
#include <dlib/array.h>
#include <dlib/hash.h>
#include <dlib/log.h>
#include <dlib/message.h>
#include <dlib/profile.h>
#include <dlib/dstrings.h>
#include <gui/gui.h>
#include <graphics/graphics_device.h>
#include <render/render.h>

#include "../resources/res_gui.h"

namespace dmGameSystem
{
    struct Component
    {
        dmGui::HScene                    m_Scene;
        uint16_t m_Enabled : 1;
    };

    struct GuiWorld
    {
        dmGui::HGui         m_Gui;
        dmArray<Component*> m_Components;
        uint32_t            m_Socket;
    };

    dmGameObject::CreateResult CompGuiNewWorld(void* context, void** world)
    {
        char socket_name[32];

        GuiWorld* gui_world = new GuiWorld();
        DM_SNPRINTF(socket_name, sizeof(socket_name), "dmgui_from_%X", (unsigned int) gui_world);
        dmMessage::Result mr = dmMessage::NewSocket(socket_name, &gui_world->m_Socket);
        if (mr != dmMessage::RESULT_OK)
        {
            dmLogFatal("Unable to create gui socket: %s (%d)", socket_name, mr);
            delete gui_world;
            return dmGameObject::CREATE_RESULT_UNKNOWN_ERROR;
        }

        dmGui::NewGuiParams gui_params;
        gui_params.m_Socket = gui_world->m_Socket;
        gui_world->m_Gui = dmGui::New(&gui_params);
        gui_world->m_Components.SetCapacity(16);
        *world = gui_world;
        return dmGameObject::CREATE_RESULT_OK;
    }

    dmGameObject::CreateResult CompGuiDeleteWorld(void* context, void* world)
    {
        GuiWorld* gui_world = (GuiWorld*)world;
        if (0 < gui_world->m_Components.Size())
        {
            dmLogWarning("%d gui component(s) were not destroyed at gui context destruction.", gui_world->m_Components.Size());
            for (uint32_t i = 0; i < gui_world->m_Components.Size(); ++i)
            {
                delete gui_world->m_Components[i];
            }
        }
        dmGui::Delete(gui_world->m_Gui);
        dmMessage::DeleteSocket(gui_world->m_Socket);
        delete gui_world;
        return dmGameObject::CREATE_RESULT_OK;
    }

    dmGameObject::CreateResult CompGuiCreate(dmGameObject::HCollection collection,
                                             dmGameObject::HInstance instance,
                                             void* resource,
                                             void* world,
                                             void* context,
                                             uintptr_t* user_data)
    {
        GuiWorld* gui_world = (GuiWorld*)world;

        GuiScenePrototype* scene_prototype = (GuiScenePrototype*) resource;

        dmGui::NewSceneParams params;
        params.m_UserData = instance;
        dmGui::HScene scene = dmGui::NewScene(gui_world->m_Gui, &params);

        // NOTE: We ignore errors here in order to be able to reload invalid scripts
        dmGui::SetSceneScript(scene, scene_prototype->m_Script, strlen(scene_prototype->m_Script), scene_prototype->m_Path);

        Component* gui_component = new Component();
        gui_component->m_Scene = scene;
        gui_component->m_Enabled = 1;

        for (uint32_t i = 0; i < scene_prototype->m_Fonts.Size(); ++i)
        {
            dmGui::AddFont(scene, scene_prototype->m_SceneDesc->m_Fonts[i].m_Name, (void*)scene_prototype->m_Fonts[i]);
        }

        *user_data = (uintptr_t)gui_component;
        gui_world->m_Components.Push(gui_component);
        return dmGameObject::CREATE_RESULT_OK;
    }

    dmGameObject::CreateResult CompGuiInit(dmGameObject::HCollection collection,
                                           dmGameObject::HInstance instance,
                                           void* context,
                                           uintptr_t* user_data)
    {
        return dmGameObject::CREATE_RESULT_OK;
    }

    dmGameObject::CreateResult CompGuiDestroy(dmGameObject::HCollection collection,
                                              dmGameObject::HInstance instance,
                                              void* world,
                                              void* context,
                                              uintptr_t* user_data)
    {
        GuiWorld* gui_world = (GuiWorld*)world;
        Component* gui_component = (Component*)*user_data;
        for (uint32_t i = 0; i < gui_world->m_Components.Size(); ++i)
        {
            if (gui_world->m_Components[i] == gui_component)
            {
                dmGui::DeleteScene(gui_component->m_Scene);
                delete gui_component;
                gui_world->m_Components.EraseSwap(i);
                break;
            }
        }
        return dmGameObject::CREATE_RESULT_OK;
    }

    void DispatchGui(dmMessage::Message *message_object, void* user_ptr)
    {
        DM_PROFILE(Game, "DispatchGui");

        dmGameObject::HRegister regist = (dmGameObject::HRegister) user_ptr;
        dmGui::MessageData* gui_message = (dmGui::MessageData*) message_object->m_Data;
        dmGameObject::HInstance instance = (dmGameObject::HInstance) dmGui::GetSceneUserData(gui_message->m_Scene);
        assert(instance);

        dmGameObject::InstanceMessageData data;
        data.m_Component = 0xff;
        data.m_DDFDescriptor = 0x0;
        data.m_MessageId = gui_message->m_MessageId;
        data.m_Instance = instance;
        dmMessage::HSocket socket = dmGameObject::GetReplyMessageSocket(regist);
        uint32_t message = dmGameObject::GetMessageId(regist);
        dmMessage::Post(socket, message, &data, sizeof(dmGameObject::InstanceMessageData));
    }

    void RenderNode(dmGui::HScene scene,
                    const dmGui::Node* nodes,
                    uint32_t node_count,
                    void* context)
    {
        dmRender::HRenderContext render_context = (dmRender::HRenderContext)context;
        for (uint32_t i = 0; i < node_count; ++i)
        {
            const dmGui::Node* node = &nodes[i];

            Vector4 pos = node->m_Properties[dmGui::PROPERTY_POSITION];
            Vector4 rot = node->m_Properties[dmGui::PROPERTY_ROTATION];
            Vector4 ext = node->m_Properties[dmGui::PROPERTY_EXTENTS];
            Vector4 color = node->m_Properties[dmGui::PROPERTY_COLOR];
            glColor4f(color.getX(), color.getY(), color.getZ(), color.getW());

            dmGui::BlendMode blend_mode = (dmGui::BlendMode) node->m_BlendMode;
            dmGraphics::HContext gfx_context = dmGraphics::GetContext();
            switch (blend_mode)
            {
                case dmGui::BLEND_MODE_ALPHA:
                    dmGraphics::SetBlendFunc(gfx_context, dmGraphics::BLEND_FACTOR_SRC_ALPHA, dmGraphics::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA);
                break;

                case dmGui::BLEND_MODE_ADD:
                    dmGraphics::SetBlendFunc(gfx_context, dmGraphics::BLEND_FACTOR_ONE, dmGraphics::BLEND_FACTOR_ONE);
                break;

                case dmGui::BLEND_MODE_ADD_ALPHA:
                    dmGraphics::SetBlendFunc(gfx_context, dmGraphics::BLEND_FACTOR_ONE, dmGraphics::BLEND_FACTOR_SRC_ALPHA);
                    break;

                case dmGui::BLEND_MODE_MULT:
                    dmGraphics::SetBlendFunc(gfx_context, dmGraphics::BLEND_FACTOR_ZERO, dmGraphics::BLEND_FACTOR_SRC_COLOR);
                    break;

                default:
                    dmLogError("Unknown blend mode: %d\n", blend_mode);
                    break;
            }

            if(node->m_NodeType == dmGui::NODE_TYPE_BOX)
            {
                Matrix4 m = Matrix4::orthographic( -1, 1, 1, -1, 10, -10 );

                Point3 p = Point3(pos.getXYZ());
                p.setX(p.getX() / 960.0f);
                p.setY(p.getY() / 540.0f);

                Vector3 s = Vector3(ext.getXYZ());
                s.setX(s.getX() / 960.0f);
                s.setY(s.getY() / 540.0f);

                dmRender::Square2d(render_context, p.getX() - s.getX(), p.getY() - s.getY(), p.getX() + s.getX(), p.getY() + s.getY(), color);
            }
            else if(node->m_NodeType == dmGui::NODE_TYPE_TEXT)
            {
                dmRender::HFont font = (dmRender::HFont) node->m_Font;
                if (font && node->m_Text)
                {
                    dmRender::DrawTextParams params;
                    params.m_Text = node->m_Text;
                    params.m_X = pos.getX();
                    params.m_Y = pos.getY();
                    params.m_FaceColor = color;
                    dmRender::DrawText(render_context, font, params);
                }
            }
        }
    }

    dmGameObject::UpdateResult CompGuiUpdate(dmGameObject::HCollection collection,
                                             const dmGameObject::UpdateContext* update_context,
                                             void* world,
                                             void* context)
    {
        GuiWorld* gui_world = (GuiWorld*)world;
        dmRender::HRenderContext render_context = (dmRender::HRenderContext)context;

        dmGameObject::HRegister regist = dmGameObject::GetRegister(collection);
        dmMessage::Dispatch(gui_world->m_Socket, &DispatchGui, regist);


        // update
        for (uint32_t i = 0; i < gui_world->m_Components.Size(); ++i)
        {
            if (gui_world->m_Components[i]->m_Enabled)
                dmGui::UpdateScene(gui_world->m_Components[i]->m_Scene, update_context->m_DT);
        }

        for (uint32_t i = 0; i < gui_world->m_Components.Size(); ++i)
        {
            Component* c = gui_world->m_Components[i];
            if (c->m_Enabled)
                dmGui::RenderScene(c->m_Scene, &RenderNode, render_context);
        }

        return dmGameObject::UPDATE_RESULT_OK;
    }

    dmGameObject::UpdateResult CompGuiOnMessage(dmGameObject::HInstance instance,
            const dmGameObject::InstanceMessageData* message_data,
            void* context,
            uintptr_t* user_data)
    {
        Component* gui_component = (Component*)*user_data;
        if (message_data->m_MessageId == dmHashString32("enable"))
        {
            gui_component->m_Enabled = 1;
        }
        else if (message_data->m_MessageId == dmHashString32("disable"))
        {
            gui_component->m_Enabled = 0;
        }
        else if (message_data->m_DDFDescriptor)
        {
            dmGui::DispatchMessage(gui_component->m_Scene, message_data->m_MessageId, (const void*) message_data->m_Buffer, message_data->m_DDFDescriptor);
        }
        return dmGameObject::UPDATE_RESULT_OK;
    }

    dmGameObject::InputResult CompGuiOnInput(dmGameObject::HInstance instance,
            const dmGameObject::InputAction* input_action,
            void* context,
            uintptr_t* user_data)
    {
        Component* gui_component = (Component*)*user_data;

        dmGui::InputAction gui_input_action;
        gui_input_action.m_ActionId = input_action->m_ActionId;
        gui_input_action.m_Value = input_action->m_Value;
        gui_input_action.m_Pressed = input_action->m_Pressed;
        gui_input_action.m_Released = input_action->m_Released;
        gui_input_action.m_Repeated = input_action->m_Repeated;

        dmGui::DispatchInput(gui_component->m_Scene, &gui_input_action, 1);
        return dmGameObject::INPUT_RESULT_IGNORED;
    }
}
