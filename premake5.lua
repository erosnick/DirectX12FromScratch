--workspace: 对应VS中的解决方案
workspace "DirectX12FromScratch"
    configurations { "Debug", "Release" }    --解决方案配置项，Debug和Release默认配置
    location "."                              --解决方案文件夹，这是我自己喜欢用的project简写
    cppdialect "c++17"
    -- Turn on DPI awareness
    dpiawareness "High"

    --增加平台配置，我希望有Win32和x64两种平台
    platforms
    {
        "Win32",
        "x64"
    }

    --Win32平台配置属性
    filter "platforms:Win32"
        architecture "x86"      --指定架构为x86
        targetdir ("bin/%{cfg.buildcfg}_%{cfg.platform}/")      --指定输出目录
        objdir  ("obj/%{cfg.buildcfg}_%{cfg.platform}/")        --指定中间目录

    --x64平台配置属性
    filter "platforms:x64"
        architecture "x64"      --指定架构为x64
        targetdir ("bin/%{cfg.buildcfg}_%{cfg.platform}/")      --指定输出目录
        objdir  ("obj/%{cfg.buildcfg}_%{cfg.platform}/")        --指定中间目录

--project: 对应VS中的项目
project "DirectX12FromScratch"
    kind "WindowedApp"                       --项目类型，控制台程序
    language "C++"                          --工程采用的语言，Premake5.0当前支持C、C++、C#
    location "Project"
    -- nuget { "WinPixEventRuntime:1.0.220810001" }
    pchheader "pch.h"
    pchsource "src/pch.cpp"

    files 
    { 
        "src/**.h", 
        "src/**.cpp",
        'src/**.rc', 
        'src/**.ico',
        'ThirdParty/imgui-console/src/**.cpp',
        'ThirdParty/D3D12MemoryAllocator/D3D12MemAlloc.cpp',
        'Assets/Shaders/**.hlsl'
    }                                       --指定加载哪些文件或哪些类型的文件

    -- removefiles 
    -- {         
    --     "%{prj.name}/src/1.Triangle.cpp"
    -- }

    vpaths 
    {
        -- ["Headers/*"] = { "*.h", "*.hpp" },  --包含具体路径
        -- ["Sources/*"] = { "*.c", "*.cpp" },  --包含具体路径
        {["Resources"] = {"**.rc", "**.ico"} },   --不包含具体路径
        {["Resources/Shaders"] = {"**.hlsl"} },   --不包含具体路径
        {["Headers"] = {"**.h", "**.hpp"} },     --不包含具体路径
        {["Sources"] = {"**.c", "**.cpp"} },     --不包含具体路径
        {["Docs"] = "**.md"}
    }

        -- Exclude template files
    filter { "files:**Chapter**.cpp" }
        -- buildaction("None")
        flags {"ExcludeFromBuild"}

    filter { "files:**.hlsl" }
        -- buildaction("None")
        flags {"ExcludeFromBuild"}

    -- excludes "files:%{prj.name}/src/1.Triangle.cpp"

    filter { "files:ThirdParty/**cpp" }
        flags {"NoPCH"}

    --Debug配置项属性
    filter "configurations:Debug"
        defines { "DEBUG", "FMT_HEADER_ONLY", "NOMINMAX" }                 --定义Debug宏(这可以算是默认配置)
        symbols "On"                        --开启调试符号
        includedirs 
        { 
            'src',
            'ThirdParty',
            'ThirdParty/stb',
            -- './ThirdParty/imgui-1.89.2',
            'ThirdParty/imgui-docking',
            'Tools/dxc_2022_12_16/inc',
            'ThirdParty/tinyobjloader',
            'ThirdParty/DirectXMath/Inc',
            'ThirdParty/glm-0.9.9.8/glm',
            'ThirdParty/fmt-9.1.0/include',
            'ThirdParty/imgui-console/include',
            'ThirdParty/imgui-console/include/imgui_console',
            'ThirdParty/glfw-3.3.8.bin.WIN64/include',
            'ThirdParty/DirectX-Headers/include/directx',
            'ThirdParty/WinPixEventRuntime.1.0.220810001/Include/WinPixEventRuntime'
        }

		libdirs 
        { 
            "ThirdParty/dxc/lib/x64",
            "Tools/dxc_2022_12_16/lib/x64",
            "ThirdParty/glfw-3.3.8.bin.WIN64/lib-vc2022",
            "ThirdParty/WinPixEventRuntime.1.0.220810001/bin/x64"
        }

		links 
        {
            "ImGui" ,
            "dxgi.lib", 
            "glfw3.lib",
            "dxguid.lib",
            "d3d12.lib",
            "dxcompiler.lib",
            "d3dcompiler.lib",
            "WinPixEventRuntime.lib"
        }

        -- debugdir "%{prj.location}"  -- 项目所在目录(.vcxproj)
        debugdir "%{wks.location}"     -- 解决方案所在目录(.sln)

    --Release配置项属性
    filter "configurations:Release"
        defines { "NDEBUG", "FMT_HEADER_ONLY", "NOMINMAX" }                 --定义NDebug宏(这可以算是默认配置)
        optimize "On"                        --开启优化参数
        includedirs 
        { 
            'src',
            'ThirdParty',
            'ThirdParty/stb',
            -- './ThirdParty/imgui-1.89.2',
            'ThirdParty/imgui-docking',
            'Tools/dxc_2022_12_16/inc',
            'ThirdParty/tinyobjloader',
            'ThirdParty/glm-0.9.9.8/glm',
            'ThirdParty/fmt-9.1.0/include',
            'ThirdParty/DirectXMath/Inc',
            'ThirdParty/imgui-console/include',
            'ThirdParty/imgui-console/include/imgui_console',
            'ThirdParty/glfw-3.3.8.bin.WIN64/include',
            'ThirdParty/DirectX-Headers/include/directx',
            'ThirdParty/WinPixEventRuntime.1.0.220810001/Include/WinPixEventRuntime'
        }

		libdirs 
        { 
            "ThirdParty/dxc/lib/x64",
            "Tools/dxc_2022_12_16/lib/x64",
            "ThirdParty/glfw-3.3.8.bin.WIN64/lib-vc2022",
            "ThirdParty/WinPixEventRuntime.1.0.220810001/bin/x64"
        }

		links 
        { 
            "ImGui" ,
            "dxgi.lib", 
            "glfw3.lib",
            "dxguid.lib",
            "d3d12.lib",
            "dxcompiler.lib",
            "d3dcompiler.lib",
            "WinPixEventRuntime.lib"
        }

        -- debugdir "%{prj.location}"  -- 项目所在目录(.vcxproj)
        debugdir "%{wks.location}"     -- 解决方案所在目录(.sln)

include "External.lua"