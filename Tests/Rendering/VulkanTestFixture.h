#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Luma/Core/Log.h"
#include "Luma/Core/LogBuffer.h"
#include "Luma/Platform/Window.h"
#include "Luma/RHI/VulkanRenderer.h"
#include "Vulkan/VulkanDevice.h"

// A real Vulkan device (hidden 64x64 window + VulkanRenderer) for backend
// tests, with the validation layer on. ValidationErrors() counts the Vulkan
// errors logged since the fixture was created.
struct VulkanTestFixture {
    std::unique_ptr<Luma::Window> window;
    std::unique_ptr<Luma::Renderer> renderer;
    Luma::VulkanDevice* device = nullptr;
    Luma::u64 logStart = 0;

    static std::shared_ptr<Luma::LogBuffer>& Log() {
        static std::shared_ptr<Luma::LogBuffer> buffer;
        return buffer;
    }

    VulkanTestFixture() {
        if (!Log()) {
            Luma::Log::Init(Luma::LogLevel::Warning);
            Log() = std::make_shared<Luma::LogBuffer>(20000);
            Luma::Log::AddSink(Log());
        }
        logStart = Log()->LatestSequence();

        Luma::WindowProps props;
        props.title = "Luma Vulkan Test";
        props.width = 64;
        props.height = 64;
        window = Luma::Window::Create(props);

        Luma::RendererConfig rc;
        rc.appName = "Luma Vulkan Test";
        rc.enableValidation = true;
        rc.vsync = false;
        renderer = Luma::CreateVulkanRenderer(*window, rc);
        device = static_cast<Luma::VulkanDevice*>(renderer->GetVulkanDevice());
    }

    ~VulkanTestFixture() {
        if (renderer) renderer->WaitIdle();
    }

    VkPhysicalDevice Physical() const { return device->Physical(); }
    VkDevice Logical() const { return device->Logical(); }
    VkQueue Queue() const { return device->GraphicsQueue(); }
    Luma::u32 QueueFamily() const { return *device->Queues().graphics; }

    std::vector<std::string> ValidationErrors() const {
        std::vector<Luma::LogEntry> entries;
        Log()->Fetch(logStart, entries);
        std::vector<std::string> errors;
        for (const auto& e : entries) {
            if (e.category == "Vulkan" && e.level == Luma::LogLevel::Error)
                errors.push_back(e.message);
        }
        return errors;
    }
};
