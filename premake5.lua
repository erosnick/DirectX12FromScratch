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
    pchsource "%{prj.name}/src/pch.cpp"

    files 
    { 
        "%{prj.name}/src/**.h", 
        "%{prj.name}/src/**.cpp",
        '%{prj.name}/src/**.rc', 
        '%{prj.name}/src/**.ico'
    }                                       --指定加载哪些文件或哪些类型的文件

    -- removefiles 
    -- {         
    --     "%{prj.name}/src/1.Triangle.cpp"
    -- }

    vpaths 
    {
        -- ["Headers/*"] = { "*.h", "*.hpp" },  --包含具体路径
        -- ["Sources/*"] = { "*.c", "*.cpp" },  --包含具体路径
        ["Headers"] = { "**.h", "**.hpp" },     --不包含具体路径
        ["Sources"] = { "**.c", "**.cpp" },     --不包含具体路径
        ["Resources"] = { "**.rc", "**.ico" },   --不包含具体路径
        ["Docs"] = "**.md"
    }

        -- Exclude template files
    filter { "files:**Chapter**.cpp" }
        -- buildaction("None")
        flags {"ExcludeFromBuild"}

    -- excludes "files:%{prj.name}/src/1.Triangle.cpp"


    --Debug配置项属性
    filter "configurations:Debug"
        defines { "DEBUG" }                 --定义Debug宏(这可以算是默认配置)
        symbols "On"                        --开启调试符号
        includedirs 
        { 
            '%{prj.name}/src',
            './ThirdParty/stb',
            './ThirdParty/imgui-1.89.2',
            './ThirdParty/glm-0.9.9.8/glm',
            './ThirdParty/DirectXMath/Inc',
            './ThirdParty/DirectX-Headers/include/directx',
            './ThirdParty/WinPixEventRuntime.1.0.220810001/Include/WinPixEventRuntime'
        }

		libdirs 
        { 
            "./ThirdParty/WinPixEventRuntime.1.0.220810001/bin/x64"
        }

		links 
        {
            "ImGui" ,
            "dxgi.lib", 
            "dxguid.lib",
            "d3d12.lib",
            "d3dcompiler.lib",
            "WinPixEventRuntime.lib"
        }

        -- debugdir "%{prj.location}"  -- 项目所在目录(.vcxproj)
        debugdir "%{wks.location}"     -- 解决方案所在目录(.sln)

    --Release配置项属性
    filter "configurations:Release"
        defines { "NDEBUG" }                 --定义NDebug宏(这可以算是默认配置)
        optimize "On"                        --开启优化参数
        includedirs 
        { 
            '%{prj.name}/src',
            './ThirdParty/stb',
            './ThirdParty/imgui-1.89.2',
            './ThirdParty/glm-0.9.9.8/glm',
            './ThirdParty/DirectXMath/Inc',
            './ThirdParty/DirectX-Headers/include/directx',
            './ThirdParty/WinPixEventRuntime.1.0.220810001/Include/WinPixEventRuntime'
        }

		libdirs 
        { 
            "./ThirdParty/WinPixEventRuntime.1.0.220810001/bin/x64"
        }

		links 
        { 
            "ImGui" ,
            "dxgi.lib", 
            "dxguid.lib",
            "d3d12.lib",
            "d3dcompiler.lib",            
            "WinPixEventRuntime.lib"
        }

        -- debugdir "%{prj.location}"  -- 项目所在目录(.vcxproj)
        debugdir "%{wks.location}"     -- 解决方案所在目录(.sln)

include "External.lua"