#include <gtest/gtest.h>
#include <stdio.h>
#include <algorithm>
#include <map>

#include <dlib/dstrings.h>
#include <dlib/log.h>
#include <dlib/math.h>

#include <ddf/ddf.h>

#include "../particle.h"
#include "../particle_private.h"

using namespace Vectormath::Aos;

class ParticleTest : public ::testing::Test
{
protected:
    virtual void SetUp()
    {
        m_Context = dmParticle::CreateContext(64, 1024);
        assert(m_Context != 0);
        m_VertexBufferSize = dmParticle::GetVertexBufferSize(1024);
        m_VertexBuffer = new uint8_t[m_VertexBufferSize];
        m_Prototype = 0x0;
    }

    virtual void TearDown()
    {
        if (m_Prototype != 0x0)
        {
            dmParticle::Particle_DeletePrototype(m_Prototype);
        }
        dmParticle::DestroyContext(m_Context);
        delete [] m_VertexBuffer;
    }

    void VerifyVertexTexCoords(dmParticle::Vertex* vertex_buffer, float* tex_coords, uint32_t tile);
    void VerifyVertexDims(dmParticle::Vertex* vertex_buffer, uint32_t particle_count, float size, uint32_t tile_width, uint32_t tile_height);

    dmParticle::HContext m_Context;
    dmParticle::HPrototype m_Prototype;
    uint8_t* m_VertexBuffer;
    uint32_t m_VertexBufferSize;
};

static const float EPSILON = 0.000001f;

// tile is 0-based
void ParticleTest::VerifyVertexTexCoords(dmParticle::Vertex* vertex_buffer, float* tex_coords, uint32_t tile)
{
    float* tc = &tex_coords[tile * 4];
    uint16_t u0 = tc[0] * 65535.0f;
    uint16_t v0 = tc[1] * 65535.0f;
    uint16_t u1 = tc[2] * 65535.0f;
    uint16_t v1 = tc[3] * 65535.0f;
    // The particle vertices are ordered like an N, where the first triangle is the lower left, second is upper right
    ASSERT_EQ(u0, vertex_buffer[0].m_U);
    ASSERT_EQ(v1, vertex_buffer[0].m_V);
    ASSERT_EQ(u0, vertex_buffer[1].m_U);
    ASSERT_EQ(v0, vertex_buffer[1].m_V);
    ASSERT_EQ(u1, vertex_buffer[2].m_U);
    ASSERT_EQ(v1, vertex_buffer[2].m_V);
    ASSERT_EQ(u1, vertex_buffer[3].m_U);
    ASSERT_EQ(v1, vertex_buffer[3].m_V);
    ASSERT_EQ(u0, vertex_buffer[4].m_U);
    ASSERT_EQ(v0, vertex_buffer[4].m_V);
    ASSERT_EQ(u1, vertex_buffer[5].m_U);
    ASSERT_EQ(v0, vertex_buffer[5].m_V);
}

void ParticleTest::VerifyVertexDims(dmParticle::Vertex* vertex_buffer, uint32_t particle_count, float size, uint32_t tile_width, uint32_t tile_height)
{
    float width_factor = 1.0f;
    float height_factor = 1.0f;
    if (tile_width > tile_height)
    {
        height_factor = tile_height / (float)tile_width;
    }
    else
    {
        width_factor = tile_width / (float)tile_height;
    }
    for (uint32_t i = 0; i < particle_count; ++i)
    {
        dmParticle::Vertex* v = &vertex_buffer[i*6];
        float x = v[0].m_X - v[2].m_X;
        float y = v[0].m_Y - v[2].m_Y;
        float w = sqrt(x * x + y * y);
        ASSERT_NEAR(size * width_factor, w, 0.000001f);
        x = v[0].m_X - v[1].m_X;
        y = v[0].m_Y - v[1].m_Y;
        float h = sqrt(x * x + y * y);
        ASSERT_NEAR(size * height_factor, h, 0.000001f);
    }
}

dmParticle::Emitter* GetEmitter(dmParticle::HContext context, dmParticle::HInstance instance, uint32_t index)
{
    return &context->m_Instances[instance & 0xffff]->m_Emitters[index];
}

bool IsSleeping(dmParticle::Emitter* emitter)
{
    return emitter->m_State == dmParticle::EMITTER_STATE_SLEEPING;
}

bool IsSpawning(dmParticle::Emitter* emitter)
{
    return emitter->m_State == dmParticle::EMITTER_STATE_SPAWNING;
}

uint32_t ParticleCount(dmParticle::Emitter* emitter)
{
    return emitter->m_Particles.Size();
}

bool LoadPrototype(const char* filename, dmParticle::HPrototype* prototype)
{
    char path[64];
    DM_SNPRINTF(path, 64, "build/default/src/test/%s", filename);
    const uint32_t MAX_FILE_SIZE = 4 * 1024;
    unsigned char buffer[MAX_FILE_SIZE];
    uint32_t file_size = 0;

    FILE* f = fopen(path, "rb");
    if (f)
    {
        file_size = fread(buffer, 1, MAX_FILE_SIZE, f);
        *prototype = dmParticle::NewPrototype(buffer, file_size);
        fclose(f);
        return *prototype != 0x0;
    }
    else
    {
        dmLogWarning("Particle FX could not be loaded: %s.", path);
        return false;
    }
}

bool ReloadPrototype(const char* filename, dmParticle::HPrototype prototype)
{
    char path[64];
    DM_SNPRINTF(path, 64, "build/default/src/test/%s", filename);
    const uint32_t MAX_FILE_SIZE = 4 * 1024;
    unsigned char buffer[MAX_FILE_SIZE];
    uint32_t file_size = 0;

    FILE* f = fopen(path, "rb");
    if (f)
    {
        file_size = fread(buffer, 1, MAX_FILE_SIZE, f);
        bool result = dmParticle::ReloadPrototype(prototype, buffer, file_size);
        fclose(f);
        return result;
    }
    else
    {
        dmLogWarning("Particle FX could not be reloaded: %s.", path);
        return false;
    }
}

struct RenderData
{
    void* m_Material;
    void* m_Texture;
    dmParticleDDF::BlendMode m_BlendMode;
    uint32_t m_VertexIndex;
    uint32_t m_VertexCount;
};

void RenderInstanceCallback(void* usercontext, void* material, void* texture, dmParticleDDF::BlendMode blendMode, uint32_t vertex_index, uint32_t vertex_count, dmParticle::RenderConstant* constants, uint32_t constant_count)
{
    RenderData* data = (RenderData*)usercontext;
    data->m_Material = material;
    data->m_Texture = texture;
    data->m_BlendMode = blendMode;
    data->m_VertexIndex = vertex_index;
    data->m_VertexCount = vertex_count;
}

TEST_F(ParticleTest, VertexBufferSize)
{
    ASSERT_EQ(6 * sizeof(dmParticle::Vertex), dmParticle::GetVertexBufferSize(1));
}

/**
 * Verify creation/destruction, check leaks
 */
TEST_F(ParticleTest, CreationSuccess)
{
    ASSERT_TRUE(LoadPrototype("once.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    ASSERT_NE(dmParticle::INVALID_INSTANCE, instance);
    ASSERT_TRUE(IsSleeping(m_Context, instance));
    ASSERT_EQ(1U, m_Context->m_InstanceIndexPool.Size());
    dmParticle::DestroyInstance(m_Context, instance);
    ASSERT_EQ(0U, m_Context->m_InstanceIndexPool.Size());
}

dmParticle::FetchAnimationResult EmptyFetchAnimationCallback(void* tile_source, dmhash_t animation, dmParticle::AnimationData* out_data)
{
    // Trash data to verify that this function is not called
    memset(out_data, 1, sizeof(*out_data));
    return dmParticle::FETCH_ANIMATION_UNKNOWN_ERROR;
}

dmParticle::FetchAnimationResult FailFetchAnimationCallback(void* tile_source, dmhash_t animation, dmParticle::AnimationData* out_data)
{
    return dmParticle::FETCH_ANIMATION_NOT_FOUND;
}

void EmptyRenderInstanceCallback(void* usercontext, void* material, void* texture, dmParticleDDF::BlendMode blendMode, uint32_t vertex_index, uint32_t vertex_count, dmParticle::RenderConstant* constants, uint32_t constant_count)
{
    // Trash data to verify that this function is not called
    RenderData* data = (RenderData*)usercontext;
    memset(data, 1, sizeof(*data));
}

float g_UnitTexCoords[] =
{
        0.0f, 0.0f, 1.0f, 1.0f
};

/**
 * Verify minimally specified files
 */
TEST_F(ParticleTest, IncompleteParticleFX)
{
    float dt = 1.0f / 60.0f;

    const char* files[] =
    {
            "empty.particlefxc",
            "empty_emitter.particlefxc",
            "empty_emitter.particlefxc"
    };
    bool has_emitter[] =
    {
            false,
            true,
            true
    };
    bool fetch_anim[] =
    {
            false,
            false,
            true
    };
    dmParticle::Vertex vertex_buffer[6];
    for (uint32_t i = 0; i < 3; ++i)
    {
        if (m_Prototype != 0x0)
            dmParticle::Particle_DeletePrototype(m_Prototype);
        ASSERT_TRUE(LoadPrototype(files[i], &m_Prototype));

        dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);

        dmParticle::StartInstance(m_Context, instance);

        uint32_t out_vertex_buffer_size;
        if (fetch_anim[i])
        {
            dmParticle::SetTileSource(m_Prototype, 0, (void*)0xBAADF00D);
            dmParticle::Update(m_Context, dt, (float*)vertex_buffer, sizeof(vertex_buffer), &out_vertex_buffer_size, FailFetchAnimationCallback);
        }
        else
        {
            dmParticle::Update(m_Context, dt, (float*)vertex_buffer, sizeof(vertex_buffer), &out_vertex_buffer_size, EmptyFetchAnimationCallback);
        }

        RenderData render_data;

        if (has_emitter[i])
        {
            dmParticle::Render(m_Context, &render_data, RenderInstanceCallback);
            ASSERT_EQ(sizeof(vertex_buffer), out_vertex_buffer_size);
            ASSERT_EQ((void*)0x0, render_data.m_Material);
            ASSERT_EQ((void*)0x0, render_data.m_Texture);
            VerifyVertexTexCoords((dmParticle::Vertex*)&((float*)vertex_buffer)[render_data.m_VertexIndex], g_UnitTexCoords, 0);
            ASSERT_EQ(6u, render_data.m_VertexCount);
            ASSERT_EQ((void*)0x0, render_data.m_Texture);
        }
        else
        {
            memset(&render_data, 0, sizeof(RenderData));
            dmParticle::Render(m_Context, &render_data, EmptyRenderInstanceCallback);
            ASSERT_EQ((void*)0x0, render_data.m_Material);
            ASSERT_EQ((void*)0x0, render_data.m_Texture);
            ASSERT_EQ(0u, render_data.m_VertexCount);
            ASSERT_EQ(0u, out_vertex_buffer_size);
        }

        dmParticle::DestroyInstance(m_Context, instance);
    }
}

/**
 * Verify once emitters end
 */
TEST_F(ParticleTest, Once)
{
    float dt = 1.0f;

    ASSERT_TRUE(LoadPrototype("once.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    dmParticle::Emitter* e = GetEmitter(m_Context, instance, 0);

    ASSERT_TRUE(dmParticle::IsSleeping(m_Context, instance));

    dmParticle::StartInstance(m_Context, instance);
    ASSERT_FALSE(dmParticle::IsSleeping(m_Context, instance));

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    ASSERT_EQ(1u, ParticleCount(e));

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    ASSERT_EQ(0u, ParticleCount(e));

    ASSERT_TRUE(dmParticle::IsSleeping(m_Context, instance));

    dmParticle::DestroyInstance(m_Context, instance);
}

/**
 * Verify once emitters respect delay
 */
TEST_F(ParticleTest, OnceDelay)
{
    float dt = 1.0f;

    ASSERT_TRUE(LoadPrototype("once_delay.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    dmParticle::Emitter* e = GetEmitter(m_Context, instance, 0);
    ASSERT_TRUE(dmParticle::IsSleeping(m_Context, instance));

    dmParticle::StartInstance(m_Context, instance);
    ASSERT_FALSE(dmParticle::IsSleeping(m_Context, instance));
    ASSERT_EQ(0u, ParticleCount(e));
    // delay
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    ASSERT_EQ(0u, ParticleCount(e));
    // spawn
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    ASSERT_EQ(1u, ParticleCount(e));
    // wait for particle to die
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    ASSERT_EQ(0u, ParticleCount(e));
    ASSERT_TRUE(dmParticle::IsSleeping(m_Context, instance));

    dmParticle::DestroyInstance(m_Context, instance);
}

/**
 * Verify loop emitters don't end
 */
TEST_F(ParticleTest, Loop)
{
    const uint32_t loop_count = 4;
    float dt = 1.0f;

    ASSERT_TRUE(LoadPrototype("loop.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    ASSERT_TRUE(dmParticle::IsSleeping(m_Context, instance));

    dmParticle::Emitter* e = GetEmitter(m_Context, instance, 0);

    dmParticle::StartInstance(m_Context, instance);
    ASSERT_FALSE(dmParticle::IsSleeping(m_Context, instance));

    for (uint32_t i = 0; i < loop_count; ++i)
    {
        dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
        ASSERT_EQ(1u, ParticleCount(e));
    }

    ASSERT_FALSE(dmParticle::IsSleeping(m_Context, instance));

    dmParticle::StopInstance(m_Context, instance);
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    ASSERT_EQ(0u, ParticleCount(e));

    dmParticle::DestroyInstance(m_Context, instance);
}

/**
 * Verify loop emitters respect delay
 */
TEST_F(ParticleTest, LoopDelay)
{
    const uint32_t loop_count = 4;
    float dt = 1.0f;

    ASSERT_TRUE(LoadPrototype("loop_delay.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    ASSERT_TRUE(dmParticle::IsSleeping(m_Context, instance));

    dmParticle::Emitter* e = GetEmitter(m_Context, instance, 0);

    dmParticle::StartInstance(m_Context, instance);
    ASSERT_FALSE(dmParticle::IsSleeping(m_Context, instance));

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    ASSERT_EQ(0u, ParticleCount(e));

    for (uint32_t i = 0; i < loop_count; ++i)
    {
        dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
        ASSERT_EQ(1u, ParticleCount(e));
    }

    ASSERT_FALSE(dmParticle::IsSleeping(m_Context, instance));

    dmParticle::StopInstance(m_Context, instance);
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    ASSERT_EQ(0u, ParticleCount(e));

    dmParticle::DestroyInstance(m_Context, instance);
}

/**
 * Verify reset
 */
TEST_F(ParticleTest, Reset)
{
    float dt = 1.0f;

    ASSERT_TRUE(LoadPrototype("once.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);

    dmParticle::Emitter* e = GetEmitter(m_Context, instance, 0);

    for (uint32_t i = 0; i < 2; ++i)
    {
        dmParticle::StartInstance(m_Context, instance);
        ASSERT_FALSE(IsSleeping(e));

        dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
        ASSERT_EQ(1U, e->m_Particles.Size());

        dmParticle::ResetInstance(m_Context, instance);
        ASSERT_TRUE(IsSleeping(e));
        ASSERT_EQ(0U, e->m_Particles.Size());
    }

    dmParticle::DestroyInstance(m_Context, instance);
}

/**
 * Verify that particles exist in the correct space
 */
TEST_F(ParticleTest, EmissionSpace)
{
    float dt = 1.0f;

    // Test world space

    ASSERT_TRUE(LoadPrototype("world_space.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);

    dmParticle::SetPosition(m_Context, instance, Vectormath::Aos::Point3(10.0f, 0.0f, 0.0f));
    dmParticle::StartInstance(m_Context, instance);

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);

    dmParticle::Emitter* e = GetEmitter(m_Context, instance, 0);
    dmParticle::Particle* p = &e->m_Particles[0];
    ASSERT_EQ(10.0f, p->GetPosition().getX());

    dmParticle::DestroyInstance(m_Context, instance);
    dmParticle::Particle_DeletePrototype(m_Prototype);

    // Test emitter space

    ASSERT_TRUE(LoadPrototype("emitter_space.particlefxc", &m_Prototype));
    instance = dmParticle::CreateInstance(m_Context, m_Prototype);

    dmParticle::SetPosition(m_Context, instance, Vectormath::Aos::Point3(10.0f, 0.0f, 0.0f));
    dmParticle::StartInstance(m_Context, instance);

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);

    e = GetEmitter(m_Context, instance, 0);
    p = &e->m_Particles[0];
    ASSERT_EQ(0.0f, p->GetPosition().getX());

    dmParticle::DestroyInstance(m_Context, instance);
}

/**
 * Verify particle life time
 */
TEST_F(ParticleTest, ParticleLife)
{
    float dt = 1.0f;

    ASSERT_TRUE(LoadPrototype("particle_life.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    dmParticle::Emitter* e = GetEmitter(m_Context, instance, 0);

    dmParticle::StartInstance(m_Context, instance);

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);

    ASSERT_EQ(0.0f, e->m_Particles[0].GetTimeLeft());

    dmParticle::DestroyInstance(m_Context, instance);
}

/**
 * Verify rate of > 1/dt
 */
TEST_F(ParticleTest, RateMulti)
{
    float dt = 1.0f;

    ASSERT_TRUE(LoadPrototype("rate_multi.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    dmParticle::Emitter* e = GetEmitter(m_Context, instance, 0);

    dmParticle::StartInstance(m_Context, instance);

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);

    ASSERT_EQ(10u, ParticleCount(e));

    dmParticle::DestroyInstance(m_Context, instance);
}

/**
 * Verify rate of < 1/dt
 */
TEST_F(ParticleTest, RateSubDT)
{
    uint32_t samples = 2;
    float dt = 1.0f / samples;

    ASSERT_TRUE(LoadPrototype("rate_sub_dt.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    dmParticle::Emitter* e = GetEmitter(m_Context, instance, 0);

    dmParticle::StartInstance(m_Context, instance);

    for (uint32_t i = 0; i < samples; ++i)
    {
        dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    }

    ASSERT_EQ(1u, ParticleCount(e));

    dmParticle::DestroyInstance(m_Context, instance);
}

/**
 * Verify total amount of spawned particles over time
 */
TEST_F(ParticleTest, RateTotal)
{
    float dt = 1.0f;
    uint32_t samples = 4;

    ASSERT_TRUE(LoadPrototype("rate_total.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    dmParticle::Emitter* e = GetEmitter(m_Context, instance, 0);

    dmParticle::StartInstance(m_Context, instance);

    for (uint32_t i = 0; i < samples; ++i)
    {
        dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    }

    ASSERT_EQ(10u, ParticleCount(e));

    dmParticle::DestroyInstance(m_Context, instance);
}

/**
 * Verify that the max particle count is respected
 */
TEST_F(ParticleTest, MaxCount)
{
    float dt = 1.0f;
    uint32_t samples = 4;

    ASSERT_TRUE(LoadPrototype("max_count.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    dmParticle::Emitter* e = GetEmitter(m_Context, instance, 0);

    dmParticle::StartInstance(m_Context, instance);

    for (uint32_t i = 0; i < samples; ++i)
    {
        dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    }

    ASSERT_EQ(5u, ParticleCount(e));

    dmParticle::DestroyInstance(m_Context, instance);
}

/**
 * The emitter has a spline for particle size, which has the points and tangents:
 * (0.00, 0), (1,0)
 * (0.25, 0), (1,1)
 * (0.50, 1), (1,0)
 * (0.75, 0), (1,-1)
 * (1.00, 0), (1,0)
 *
 * Test evaluation of the size at t = [0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1]
 */
TEST_F(ParticleTest, EvaluateEmitterProperty)
{
    float dt = 1.0f / 8.0f;

    ASSERT_TRUE(LoadPrototype("emitter_spline.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    dmParticle::Emitter* e = GetEmitter(m_Context, instance, 0);
    dmParticle::StartInstance(m_Context, instance);

    // t = 0.125, size < 0
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    dmParticle::Particle* particle = &e->m_Particles[0];
    ASSERT_GT(0.0f, particle->GetSize());

    // t = 0.25, size = 0
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    ASSERT_DOUBLE_EQ(0.0f, particle->GetSize());

    // t = 0.375, size > 0
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    ASSERT_LT(0.0f, particle->GetSize());

    // t = 0.5, size = 1
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    ASSERT_DOUBLE_EQ(1.0f, particle->GetSize());

    // t = 0.625, size > 0
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    ASSERT_LT(0.0f, particle->GetSize());

    // t = 0.75, size = 0
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    ASSERT_DOUBLE_EQ(0.0f, particle->GetSize());

    // t = 0.875, size < 0
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    ASSERT_GT(0.0f, particle->GetSize());

    // t = 1, size = 0
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    ASSERT_NEAR(0.0f, particle->GetSize(), EPSILON);

    dmParticle::DestroyInstance(m_Context, instance);
}

/**
 * The emitter has a constant for particle size = 0 and spread +- 1.0
 */
TEST_F(ParticleTest, EvaluateEmitterPropertySpread)
{
    float dt = 1.0f / 8.0f;

    ASSERT_TRUE(LoadPrototype("emitter_spline_spread.particlefxc", &m_Prototype));

    for (uint32_t i = 0; i < 1000; ++i)
    {
        dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
        dmParticle::Emitter* emitter = GetEmitter(m_Context, instance, 0);

        dmParticle::StartInstance(m_Context, instance);

        dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
        dmParticle::Particle* particle = &emitter->m_Particles[0];
        // NOTE size could potentially be 0, but not likely
        ASSERT_NE(0.0f, particle->GetSize());
        ASSERT_GE(1.0f, dmMath::Abs(particle->GetSize()));

        dmParticle::DestroyInstance(m_Context, instance);
    }

}

/**
 * The emitter has a spline for particle scale (size is always 1), which has the points and tangents:
 * (0.00, 0), (1,0)
 * (0.25, 0), (1,1)
 * (0.50, 1), (1,0)
 * (0.75, 0), (1,-1)
 * (1.00, 0), (1,0)
 *
 * Test evaluation of the size at t = [0, 0.125, 0.25, 0.375, 0.5, 0.625, 0.75, 0.875, 1]
 */
TEST_F(ParticleTest, EvaluateParticleProperty)
{
    float dt = 1.0f / 8.0f;

    ASSERT_TRUE(LoadPrototype("particle_spline.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    dmParticle::Emitter* e = GetEmitter(m_Context, instance, 0);

    dmParticle::StartInstance(m_Context, instance);

    // t = 0.125, size < 0
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    dmParticle::Particle* particle = &e->m_Particles[0];
    ASSERT_GT(0.0f, particle->GetSize());

    // t = 0.25, size = 0
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    ASSERT_DOUBLE_EQ(0.0f, particle->GetSize());

    // t = 0.375, size > 0
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    ASSERT_LT(0.0f, particle->GetSize());

    // t = 0.5, size = 1
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    ASSERT_DOUBLE_EQ(1.0f, particle->GetSize());

    // t = 0.625, size > 0
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    ASSERT_LT(0.0f, particle->GetSize());

    // t = 0.75, size = 0
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    ASSERT_DOUBLE_EQ(0.0f, particle->GetSize());

    // t = 0.875, size < 0
    // Updating with a full dt here will make the emitter reach its duration
    dmParticle::Update(m_Context, dt - EPSILON, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    ASSERT_GT(0.0f, particle->GetSize());

    // t = 1, size = 0
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    ASSERT_NEAR(0.0f, particle->GetSize(), EPSILON);

    dmParticle::DestroyInstance(m_Context, instance);
}

/**
 * Verify that particles are scaled with the instance
 */
TEST_F(ParticleTest, ParticleInstanceScale)
{
    float dt = 1.0f;

    ASSERT_TRUE(LoadPrototype("instance_scale.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);

    dmParticle::SetScale(m_Context, instance, 2.0f);
    dmParticle::StartInstance(m_Context, instance);

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);

    dmParticle::Emitter* e = GetEmitter(m_Context, instance, 0);
    dmParticle::Particle* p = &e->m_Particles[0];
    ASSERT_EQ(2.0f, p->GetSize());

    dmParticle::DestroyInstance(m_Context, instance);
}

/**
 * Test that flip book animations are updated correctly
 */

float g_TexCoords[] =
{
        // 2 x 4 tiles
        0.0f, 0.0f, 0.25f, 0.5f,
        0.25f, 0.0f, 0.5f, 0.5f,
        0.5f, 0.0f, 0.75f, 0.5f,
        0.75f, 0.0f, 1.0f, 0.5f,
        0.0f, 0.5f, 0.25f, 1.0f,
        0.25f, 0.5f, 0.5f, 1.0f,
        0.5f, 0.5f, 0.75f, 1.0f,
        0.75f, 0.5f, 1.0f, 1.0f,
};

struct TileSource
{
    TileSource()
    : m_Texture((void*)0xBAADF00D)
    , m_TexCoords(g_TexCoords)
    {

    }

    void* m_Texture;
    float* m_TexCoords;
};

dmParticle::FetchAnimationResult FetchAnimationCallback(void* tile_source, dmhash_t animation, dmParticle::AnimationData* out_data)
{
    if (tile_source == 0x0)
    {
        return dmParticle::FETCH_ANIMATION_UNKNOWN_ERROR;
    }
    TileSource* ts = (TileSource*)tile_source;
    out_data->m_Texture = ts->m_Texture;
    out_data->m_TexCoords = ts->m_TexCoords;
    out_data->m_TileWidth = 2;
    out_data->m_TileHeight = 3;
    out_data->m_StartTile = 1;
    out_data->m_EndTile = 5;
    out_data->m_FPS = 4;
    out_data->m_Texture = (void*)0xBAADF00D;
    out_data->m_StructSize = sizeof(dmParticle::AnimationData);
    if (animation == dmHashString64("none"))
        out_data->m_Playback = dmParticle::ANIM_PLAYBACK_NONE;
    else if (animation == dmHashString64("once_fwd"))
        out_data->m_Playback = dmParticle::ANIM_PLAYBACK_ONCE_FORWARD;
    else if (animation == dmHashString64("once_bwd"))
        out_data->m_Playback = dmParticle::ANIM_PLAYBACK_ONCE_BACKWARD;
    else if (animation == dmHashString64("loop_fwd"))
        out_data->m_Playback = dmParticle::ANIM_PLAYBACK_LOOP_FORWARD;
    else if (animation == dmHashString64("loop_bwd"))
        out_data->m_Playback = dmParticle::ANIM_PLAYBACK_LOOP_BACKWARD;
    else if (animation == dmHashString64("pingpong"))
        out_data->m_Playback = dmParticle::ANIM_PLAYBACK_LOOP_PINGPONG;
    else
        return dmParticle::FETCH_ANIMATION_NOT_FOUND;
    return dmParticle::FETCH_ANIMATION_OK;
}

TEST_F(ParticleTest, Animation)
{
    float dt = 0.25f;

    ASSERT_TRUE(LoadPrototype("anim.particlefxc", &m_Prototype));

    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);

    // 2 types
    const uint32_t type_count = 6;

    TileSource tile_source;
    for (uint32_t emitter_i = 0; emitter_i < type_count; ++emitter_i)
        dmParticle::SetTileSource(m_Prototype, emitter_i, &tile_source);

    // 8 time steps
    const uint32_t it_count = 8;
    uint32_t tiles[type_count][it_count] = {
            {1, 1, 1, 1, 1, 0, 0, 0}, // none, 5-frame particle
            {1, 2, 3, 4, 5, 0, 0, 0}, // once fwd, 5-frame particle
            {5, 4, 3, 2, 1, 0, 0, 0}, // once bwd, 5-frame particle
            {1, 2, 3, 4, 5, 1, 2, 3}, // loop fwd, 8-frame particle
            {5, 4, 3, 2, 1, 5, 4, 3}, // loop bwd, 8-frame particle
            {1, 2, 3, 4, 5, 4, 3, 2}, // loop pingpong, 8-frame particle
    };
    dmParticle::Vertex vertex_buffer[6 * type_count];
    uint32_t vertex_buffer_size;

    dmParticle::StartInstance(m_Context, instance);

    for (uint32_t it = 0; it < it_count; ++it)
    {
        dmParticle::Update(m_Context, dt, (float*)vertex_buffer, sizeof(vertex_buffer), &vertex_buffer_size, FetchAnimationCallback);
        dmParticle::Vertex* vb = vertex_buffer;
        for (uint32_t type = 0; type < type_count; ++type)
        {
            uint32_t tile = tiles[type][it];
            if (tile > 0)
            {
                VerifyVertexTexCoords(vb, g_TexCoords, tile - 1);
                VerifyVertexDims(vb, 1, 1.0f, 2, 3);
                vb += 6;
            }
        }
        ASSERT_EQ((vb - vertex_buffer) * sizeof(dmParticle::Vertex), vertex_buffer_size);
    }

    dmParticle::DestroyInstance(m_Context, instance);
}

TEST_F(ParticleTest, InvalidKeys)
{
    ASSERT_TRUE(LoadPrototype("invalid_keys.particlefxc", &m_Prototype));
}

TEST_F(ParticleTest, StableSort)
{
    float dt = 1.0f / 60.0f;

    ASSERT_TRUE(LoadPrototype("sort.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    uint16_t index = instance & 0xffff;

    dmParticle::Instance* i = m_Context->m_Instances[index];

    dmParticle::StartInstance(m_Context, instance);

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);

    const uint32_t particle_count = 20;
    ASSERT_EQ(particle_count, i->m_Emitters[0].m_Particles.Size());

    float x[particle_count];
    dmParticle::Particle* p = &i->m_Emitters[0].m_Particles[0];
    // Store x-positions
    for (uint32_t pi = 0; pi < particle_count; ++pi)
    {
        float f = (float)pi + 1;
        x[pi] = f;
        Point3 pos = p[pi].GetPosition();
        pos.setX(f);
        p[pi].SetPosition(pos);
    }
    // Disturb order by altering a few particles
    const uint32_t disturb_count = particle_count / 2;
    for (uint32_t d = 0; d < disturb_count; ++d)
    {
        p[d].SetTimeLeft(p[d].GetTimeLeft() - dt);
        x[d] += particle_count;
        Point3 pos = p[d].GetPosition();
        pos.setX(x[d]);
        p[d].SetPosition(pos);
    }
    // Sort
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    // Sort verification
    std::sort(x, x+particle_count);
    // Verify order of undisturbed
    for (uint32_t pi = 0; pi < particle_count; ++pi)
    {
        ASSERT_EQ(x[pi], p[pi].GetPosition().getX());
    }

    dmParticle::DestroyInstance(m_Context, instance);
}

TEST_F(ParticleTest, ReloadPrototype)
{
    ASSERT_TRUE(LoadPrototype("reload1.particlefxc", &m_Prototype));
    ASSERT_EQ(1u, m_Prototype->m_Emitters.Size());

    ASSERT_TRUE(ReloadPrototype("reload2.particlefxc", m_Prototype));
    ASSERT_EQ(2u, m_Prototype->m_Emitters.Size());

    ASSERT_TRUE(ReloadPrototype("reload1.particlefxc", m_Prototype));
    ASSERT_EQ(1u, m_Prototype->m_Emitters.Size());
}

TEST_F(ParticleTest, ReloadInstance)
{
    float dt = 1.0f / 60.0f;

    ASSERT_TRUE(LoadPrototype("reload1.particlefxc", &m_Prototype));

    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    dmParticle::Emitter* e = GetEmitter(m_Context, instance, 0);

    dmParticle::StartInstance(m_Context, instance);
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);

    ASSERT_EQ(1u, e->m_Particles.Size());

    dmParticle::Particle original_particle;
    memcpy(&original_particle, &e->m_Particles[0], sizeof(dmParticle::Particle));

    uint32_t seed = e->m_Seed;
    float timer = e->m_Timer;

    ASSERT_TRUE(ReloadPrototype("reload2.particlefxc", m_Prototype));
    dmParticle::ReloadInstance(m_Context, instance, true);
    e = GetEmitter(m_Context, instance, 0);
    ASSERT_EQ(timer, e->m_Timer);
    ASSERT_EQ(seed, e->m_Seed);
    ASSERT_EQ(1u, e->m_Particles.Size());
    dmParticle::Particle* particle = &e->m_Particles[0];
    ASSERT_EQ(0, memcmp(&original_particle, particle, sizeof(dmParticle::Particle)));

    dmParticle::Emitter* e1 = GetEmitter(m_Context, instance, 1);
    ASSERT_EQ(1u, e1->m_Particles.Size());

    ASSERT_TRUE(ReloadPrototype("reload1.particlefxc", m_Prototype));
    dmParticle::ReloadInstance(m_Context, instance, true);
    e = GetEmitter(m_Context, instance, 0);

    ASSERT_EQ(1u, e->m_Particles.Size());
    particle = &e->m_Particles[0];
    ASSERT_EQ(0, memcmp(&original_particle, particle, sizeof(dmParticle::Particle)));

    // Test reload with max_particle_count changed
    ASSERT_TRUE(ReloadPrototype("reload3.particlefxc", m_Prototype));
    dmParticle::ReloadInstance(m_Context, instance, true);
    e = GetEmitter(m_Context, instance, 0);

    ASSERT_EQ(2u, e->m_Particles.Size());
    particle = &e->m_Particles[0];
    ASSERT_EQ(0, memcmp(&original_particle, particle, sizeof(dmParticle::Particle)));

    dmParticle::DestroyInstance(m_Context, instance);
}

TEST_F(ParticleTest, ReloadInstanceLoop)
{
    float dt = 1.0f / 60.0f;

    ASSERT_TRUE(LoadPrototype("reload_loop.particlefxc", &m_Prototype));

    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    dmParticle::Emitter* e = GetEmitter(m_Context, instance, 0);

    dmParticle::StartInstance(m_Context, instance);

    const float time = 0.4f;
    float timer = 0.0f;
    while (timer < time)
    {
        dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
        timer += dt;
    }

    ASSERT_EQ(1u, e->m_Particles.Size());
    float emitter_timer = e->m_Timer;

    dmParticle::Particle original_particle;
    memcpy(&original_particle, &e->m_Particles[0], sizeof(dmParticle::Particle));

    ASSERT_TRUE(ReloadPrototype("reload_loop.particlefxc", m_Prototype));
    dmParticle::ReloadInstance(m_Context, instance, true);
    e = GetEmitter(m_Context, instance, 0);

    ASSERT_EQ(1u, e->m_Particles.Size());
    ASSERT_EQ(emitter_timer, e->m_Timer);
    ASSERT_EQ(1u, e->m_Particles.Size());
    dmParticle::Particle* particle = &e->m_Particles[0];
    ASSERT_EQ(0, memcmp(&original_particle, particle, sizeof(dmParticle::Particle)));

    dmParticle::DestroyInstance(m_Context, instance);
}

/**
 * Verify that multiple emitters have the same time each time they are replayed, even when starting from a large play time.
 */
TEST_F(ParticleTest, ReplayLoopLargePlayTime)
{
    float dt = 1.0f / 60.0f;

    const uint32_t emitter_count = 3;
    float times[emitter_count];
    ASSERT_TRUE(LoadPrototype("reload_loop_multi.particlefxc", &m_Prototype));

    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);

    dmParticle::StartInstance(m_Context, instance);

    const float time = 1.1f;
    float timer = 0.0f;
    while (timer < time)
    {
        dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
        timer += dt;
    }
    dmParticle::ReloadInstance(m_Context, instance, true);
    for (uint32_t i = 0; i < emitter_count; ++i)
    {
        dmParticle::Emitter* e = GetEmitter(m_Context, instance, i);
        times[i] = e->m_Timer;
    }
    for (uint32_t i = 0; i < 3; ++i)
    {
        dmParticle::ReloadInstance(m_Context, instance, true);
        for (uint32_t emitter_i = 0; emitter_i < emitter_count; ++emitter_i)
        {
            dmParticle::Emitter* e = GetEmitter(m_Context, instance, i);
            ASSERT_EQ(times[i], e->m_Timer);
        }
    }

    dmParticle::DestroyInstance(m_Context, instance);
}

TEST_F(ParticleTest, AccelerationWorld)
{
    float dt = 1.0f;

    ASSERT_TRUE(LoadPrototype("mod_acc_world.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    uint16_t index = instance & 0xffff;
    dmParticle::Instance* i = m_Context->m_Instances[index];

    dmParticle::StartInstance(m_Context, instance);
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    dmParticle::Particle* particle = &i->m_Emitters[0].m_Particles[0];
    ASSERT_EQ(0.0f, particle->GetVelocity().getX());
    ASSERT_EQ(1.0f, particle->GetVelocity().getY());
    ASSERT_EQ(0.0f, particle->GetVelocity().getZ());

    dmParticle::SetRotation(m_Context, instance, Quat::rotationZ(M_PI * 0.5f));
    dmParticle::ResetInstance(m_Context, instance);
    dmParticle::StartInstance(m_Context, instance);
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    particle = &i->m_Emitters[0].m_Particles[0];
    ASSERT_EQ(0.0f, particle->GetVelocity().getX());
    ASSERT_EQ(1.0f, particle->GetVelocity().getY());
    ASSERT_EQ(0.0f, particle->GetVelocity().getZ());

    dmParticle::DestroyInstance(m_Context, instance);
}

TEST_F(ParticleTest, AccelerationEmitter)
{
    float dt = 1.0f;

    ASSERT_TRUE(LoadPrototype("mod_acc_em.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    uint16_t index = instance & 0xffff;
    dmParticle::Instance* i = m_Context->m_Instances[index];

    dmParticle::StartInstance(m_Context, instance);

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    dmParticle::Particle* particle = &i->m_Emitters[0].m_Particles[0];
    ASSERT_EQ(0.0f, particle->GetVelocity().getX());
    ASSERT_NEAR(1.0f, particle->GetVelocity().getY(), EPSILON);
    ASSERT_EQ(0.0f, particle->GetVelocity().getZ());

    dmParticle::SetRotation(m_Context, instance, Quat::rotationZ(M_PI));
    dmParticle::ResetInstance(m_Context, instance);
    dmParticle::StartInstance(m_Context, instance);
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    particle = &i->m_Emitters[0].m_Particles[0];
    ASSERT_EQ(0.0f, particle->GetVelocity().getX());
    ASSERT_NEAR(1.0f, particle->GetVelocity().getY(), EPSILON);
    ASSERT_EQ(0.0f, particle->GetVelocity().getZ());

    dmParticle::DestroyInstance(m_Context, instance);
}

TEST_F(ParticleTest, AccelerationAnimated)
{
    float dt = 0.25f;

    ASSERT_TRUE(LoadPrototype("mod_acc_anim.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    dmParticle::Emitter* emitter = GetEmitter(m_Context, instance, 0);

    dmParticle::StartInstance(m_Context, instance);

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    dmParticle::Particle* particle = &emitter->m_Particles[0];
    ASSERT_EQ(0.0f, particle->GetVelocity().getX());
    ASSERT_LT(0.0f, particle->GetVelocity().getY());
    ASSERT_EQ(0.0f, particle->GetVelocity().getZ());

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    // New particle at 0 because of sorting
    particle = &emitter->m_Particles[0];
    ASSERT_EQ(0.0f, lengthSqr(particle->GetVelocity()));

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    // New particle at 0 because of sorting
    particle = &emitter->m_Particles[0];
    ASSERT_EQ(0.0f, particle->GetVelocity().getX());
    ASSERT_GT(0.0f, particle->GetVelocity().getY());
    ASSERT_EQ(0.0f, particle->GetVelocity().getZ());

    dmParticle::DestroyInstance(m_Context, instance);
}

TEST_F(ParticleTest, DragNoDir)
{
    float dt = 1.0f;

    ASSERT_TRUE(LoadPrototype("mod_drag_nodir.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    uint16_t index = instance & 0xffff;
    dmParticle::Instance* i = m_Context->m_Instances[index];

    dmParticle::StartInstance(m_Context, instance);

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    dmParticle::Particle* particle = &i->m_Emitters[0].m_Particles[0];
    ASSERT_EQ(0.0f, lengthSqr(particle->GetVelocity()));

    dmParticle::DestroyInstance(m_Context, instance);
}

TEST_F(ParticleTest, DragDir)
{
    float dt = 1.0f;

    ASSERT_TRUE(LoadPrototype("mod_drag_dir.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    uint16_t index = instance & 0xffff;
    dmParticle::Instance* i = m_Context->m_Instances[index];

    dmParticle::StartInstance(m_Context, instance);

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    dmParticle::Particle* particle = &i->m_Emitters[0].m_Particles[0];
    Vector3 velocity = particle->GetVelocity();
    ASSERT_NEAR(0.0f, velocity.getX(), EPSILON);
    ASSERT_LT(0.0f, velocity.getY());
    ASSERT_EQ(0.0f, velocity.getZ());

    dmParticle::DestroyInstance(m_Context, instance);
}

TEST_F(ParticleTest, DragBigMagnitude)
{
    float dt = 1.0f / 4.0f;

    ASSERT_TRUE(LoadPrototype("mod_drag_bigmag.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    uint16_t index = instance & 0xffff;
    dmParticle::Instance* i = m_Context->m_Instances[index];

    dmParticle::StartInstance(m_Context, instance);

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    dmParticle::Particle* particle = &i->m_Emitters[0].m_Particles[0];
    ASSERT_EQ(0u, lengthSqr(particle->GetVelocity()));

    dmParticle::DestroyInstance(m_Context, instance);
}

TEST_F(ParticleTest, Radial)
{
    float dt = 1.0f;

    ASSERT_TRUE(LoadPrototype("mod_radial.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    uint16_t index = instance & 0xffff;
    dmParticle::Instance* i = m_Context->m_Instances[index];

    dmParticle::StartInstance(m_Context, instance);

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    dmParticle::Particle* particle = &i->m_Emitters[0].m_Particles[0];
    ASSERT_EQ(1.0f, lengthSqr(particle->GetVelocity()));
    ASSERT_EQ(-1.0f, particle->GetVelocity().getX());

    dmParticle::DestroyInstance(m_Context, instance);
}

TEST_F(ParticleTest, RadialMaxDistance)
{
    float dt = 1.0f;

    ASSERT_TRUE(LoadPrototype("mod_radial_maxdist.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    uint16_t index = instance & 0xffff;
    dmParticle::Instance* i = m_Context->m_Instances[index];

    dmParticle::StartInstance(m_Context, instance);

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    dmParticle::Particle* particle = &i->m_Emitters[0].m_Particles[0];
    ASSERT_EQ(0.0f, lengthSqr(particle->GetVelocity()));

    // Test with instance scale
    dmParticle::ResetInstance(m_Context, instance);
    dmParticle::SetScale(m_Context, instance, 2.0f);
    dmParticle::StartInstance(m_Context, instance);
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    particle = &i->m_Emitters[0].m_Particles[0];
    ASSERT_EQ(0.0f, lengthSqr(particle->GetVelocity()));

    dmParticle::DestroyInstance(m_Context, instance);
}

// Velocity-less particle at the same position as the radial
TEST_F(ParticleTest, RadialEdgeCase)
{
    float dt = 1.0f;

    ASSERT_TRUE(LoadPrototype("mod_radial_edgecase.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    uint16_t index = instance & 0xffff;
    dmParticle::Instance* i = m_Context->m_Instances[index];

    dmParticle::StartInstance(m_Context, instance);

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    dmParticle::Particle* particle = &i->m_Emitters[0].m_Particles[0];
    ASSERT_EQ(1.0f, lengthSqr(particle->GetVelocity()));

    dmParticle::DestroyInstance(m_Context, instance);
}

TEST_F(ParticleTest, Vortex)
{
    float dt = 1.0f;

    ASSERT_TRUE(LoadPrototype("mod_vortex.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    uint16_t index = instance & 0xffff;
    dmParticle::Instance* i = m_Context->m_Instances[index];

    dmParticle::StartInstance(m_Context, instance);

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    dmParticle::Particle* particle = &i->m_Emitters[0].m_Particles[0];
    ASSERT_EQ(0.0f, particle->GetVelocity().getX());
    ASSERT_EQ(-1.0f, particle->GetVelocity().getY());
    ASSERT_EQ(0.0f, particle->GetVelocity().getZ());

    dmParticle::DestroyInstance(m_Context, instance);
}

TEST_F(ParticleTest, VortexMaxDistance)
{
    float dt = 1.0f;

    ASSERT_TRUE(LoadPrototype("mod_vortex_maxdist.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    uint16_t index = instance & 0xffff;
    dmParticle::Instance* i = m_Context->m_Instances[index];

    dmParticle::StartInstance(m_Context, instance);

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    dmParticle::Particle* particle = &i->m_Emitters[0].m_Particles[0];
    ASSERT_EQ(0.0f, lengthSqr(particle->GetVelocity()));

    // Test with instance scale
    dmParticle::ResetInstance(m_Context, instance);
    dmParticle::SetScale(m_Context, instance, 2.0f);
    dmParticle::StartInstance(m_Context, instance);
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    particle = &i->m_Emitters[0].m_Particles[0];
    ASSERT_EQ(0.0f, lengthSqr(particle->GetVelocity()));

    dmParticle::DestroyInstance(m_Context, instance);
}

TEST_F(ParticleTest, VortexEdgeCase)
{
    float dt = 1.0f;

    ASSERT_TRUE(LoadPrototype("mod_vortex_edgecase.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    uint16_t index = instance & 0xffff;
    dmParticle::Instance* i = m_Context->m_Instances[index];

    dmParticle::StartInstance(m_Context, instance);

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    dmParticle::Particle* particle = &i->m_Emitters[0].m_Particles[0];
    ASSERT_EQ(-1.0f, particle->GetVelocity().getX());
    ASSERT_EQ(0.0f, particle->GetVelocity().getY());
    ASSERT_EQ(0.0f, particle->GetVelocity().getZ());

    dmParticle::DestroyInstance(m_Context, instance);
}

TEST_F(ParticleTest, case1544)
{
    ASSERT_TRUE(LoadPrototype("modifier_crash.particlefxc", &m_Prototype));
}

void RenderConstantRenderInstanceCallback(void* usercontext, void* material, void* texture, dmParticleDDF::BlendMode blendMode, uint32_t vertex_index, uint32_t vertex_count, dmParticle::RenderConstant* constants, uint32_t constant_count)
{
    std::map<dmhash_t, Vector4>* render_constants = (std::map<dmhash_t, Vector4>*)usercontext;
    for (uint32_t i = 0; i < constant_count; ++i)
    {
        dmParticle::RenderConstant& c = constants[i];
        (*render_constants)[c.m_NameHash] = c.m_Value;
    }
}

TEST_F(ParticleTest, RenderConstants)
{
    dmhash_t emitter_id = dmHashString64("emitter");
    dmhash_t constant_id = dmHashString64("tint");

    float dt = 1.0f / 60.0f;

    ASSERT_TRUE(LoadPrototype("render_constant.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);

    dmParticle::StartInstance(m_Context, instance);

    std::map<dmhash_t, Vector4> constants;

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);

    dmParticle::Render(m_Context, (void*)&constants, RenderConstantRenderInstanceCallback);

    ASSERT_TRUE(constants.empty());

    dmParticle::SetRenderConstant(m_Context, instance, emitter_id, constant_id, Vector4(1, 2, 3, 4));
    dmParticle::Render(m_Context, (void*)&constants, RenderConstantRenderInstanceCallback);

    ASSERT_TRUE(constants.end() != constants.find(constant_id));
    Vector4 v = constants.at(constant_id);
    ASSERT_EQ(1, v.getX());
    ASSERT_EQ(2, v.getY());
    ASSERT_EQ(3, v.getZ());
    ASSERT_EQ(4, v.getW());

    constants.clear();

    dmParticle::ResetRenderConstant(m_Context, instance, emitter_id, constant_id);
    dmParticle::Render(m_Context, (void*)&constants, RenderConstantRenderInstanceCallback);

    ASSERT_TRUE(constants.empty());

    dmParticle::DestroyInstance(m_Context, instance);
}

TEST_F(ParticleTest, InheritVelocity)
{
    float dt = 1.0f / 60.0f;

    ASSERT_TRUE(LoadPrototype("inherit_velocity.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);
    uint16_t index = instance & 0xffff;
    dmParticle::Instance* i = m_Context->m_Instances[index];
    dmParticle::Emitter* e1 = &i->m_Emitters[0];
    dmParticle::Emitter* e2 = &i->m_Emitters[1];

    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    dmParticle::StartInstance(m_Context, instance);
    dmParticle::SetPosition(m_Context, instance, Point3(10, 0, 0));
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);

    ASSERT_EQ(0.0f, lengthSqr(e1->m_Particles[0].GetVelocity()));
    ASSERT_NE(0.0f, lengthSqr(e2->m_Particles[0].GetVelocity()));

    dmParticle::DestroyInstance(m_Context, instance);
}

TEST_F(ParticleTest, Stats)
{
    float dt = 1.0f / 60.0f;

    ASSERT_TRUE(LoadPrototype("stats.particlefxc", &m_Prototype));
    dmParticle::HInstance instance = dmParticle::CreateInstance(m_Context, m_Prototype);

    dmParticle::StartInstance(m_Context, instance);
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);
    dmParticle::Update(m_Context, dt, m_VertexBuffer, m_VertexBufferSize, 0x0, 0x0);

    dmParticle::Stats stats;
    dmParticle::InstanceStats instance_stats;

    dmParticle::GetStats(m_Context, &stats);
    dmParticle::GetInstanceStats(m_Context, instance, &instance_stats);

    ASSERT_EQ(1024U, stats.m_Particles);
    ASSERT_EQ(1024U, stats.m_MaxParticles);
    ASSERT_NEAR(instance_stats.m_Time, 2 * dt, 0.001f);

    dmParticle::DestroyInstance(m_Context, instance);
}

int main(int argc, char **argv)
{
    testing::InitGoogleTest(&argc, argv);

    int ret = RUN_ALL_TESTS();
    return ret;
}
