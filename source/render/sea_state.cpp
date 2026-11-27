/**
 * @file sea_state.cpp
 * The sea's uniform buffer, its set, and the packing of the sea's numbers into it.
 */
#include "render/sea_state.hpp"

#include "water/sea.hpp"

#include <glm/vec4.hpp>

#include <array>
#include <span>

namespace
{
    /** The sea's numbers as shaders/sea_waves.glsl reads them (std140: vec4s only); see there for the parts. */
    struct GpuSea
    {
        std::array<uint32_t, 4>                       Count{};
        glm::vec4                                     Shoal{0.0f};
        glm::vec4                                     Swell{0.0f};
        glm::vec4                                     SwellClock{0.0f};
        std::array<glm::vec4, 2 * Wotah::MaxSeaWaves> Waves{};
    };

    /**
     * The set: the sea's numbers, and the floor's shape (terrain_shape.glsl), both for both stages: the water's vertex
     * shader moves the water and finds how deep it is, the fragment shaders shade the waves and the wet sand (and the
     * tests' probe works the surface out per pixel).
     */
    constexpr uint32_t           SeaBinding   = 0;
    constexpr uint32_t           FloorBinding = 1;
    constexpr VkShaderStageFlags SeaStages    = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    constexpr std::array<Wotah::DescriptorBinding, 2> SeaBindings{
        {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SeaStages}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, SeaStages}}};

    /** The sea's numbers as the shaders read them, its waves where they stand at its clock. */
    GpuSea Pack(const Wotah::Sea& sea)
    {
        GpuSea                 packed;
        const Wotah::SeaSwell& swell = sea.Waves.Swell;
        packed.Count                 = {sea.Waves.Count, sea.Floor ? 1u : 0u, 0u, 0u};
        packed.Shoal                 = {Wotah::GetShoalRate(sea.Waves), 0.0f, Wotah::WaveFloorDetail, sea.Level};
        packed.Swell                 = {swell.Direction.x, swell.Direction.y, swell.Height, swell.Period};
        packed.SwellClock            = {Wotah::GetSwellClock(swell, sea.Seconds), 0.0f, 0.0f, 0.0f};
        for (uint32_t index = 0; index < sea.Waves.Count && index < Wotah::MaxSeaWaves; ++index)
        {
            const Wotah::SeaWave& wave  = sea.Waves.Waves[index];
            packed.Waves[2 * index]     = {wave.Direction.x, wave.Direction.y, wave.Wavelength, wave.Amplitude};
            packed.Waves[2 * index + 1] = {wave.Steepness, Wotah::GetWavePhase(wave, sea.Seconds),
                                           Wotah::GetWavenumber(wave), 0.0f};
        }
        return packed;
    }
} // namespace

bool Wotah::SeaState::Create(const Device& device)
{
    Destroy(); // a second Create replaces the first instead of leaking it
    if (!m_buffer.Create(device, sizeof(GpuSea), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, BufferMemory::CpuToGpu) ||
        !m_set.Create(device, SeaBindings))
    {
        return false;
    }
    m_set.WriteBuffer(SeaBinding, m_buffer.GetHandle(), sizeof(GpuSea));
    return Update(Sea{});
}

void Wotah::SeaState::Destroy()
{
    m_set.Destroy();
    m_buffer.Destroy();
}

void Wotah::SeaState::SetFloor(const Buffer& shape)
{
    m_set.WriteBuffer(FloorBinding, shape.GetHandle(), shape.GetSize());
}

bool Wotah::SeaState::Update(const Sea& sea)
{
    const GpuSea packed = Pack(sea);
    return m_buffer.Write(std::as_bytes(std::span(&packed, 1)));
}

void Wotah::SeaState::Bind(VkCommandBuffer commandBuffer, VkPipelineLayout pipelineLayout) const
{
    m_set.Bind(commandBuffer, pipelineLayout, SeaStateSet);
}
