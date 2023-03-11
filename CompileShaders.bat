%~dp0/Tools/dxc_2022_12_16/bin/x64/dxc.exe -T vs_6_0 -E VSMain Assets/Shaders/7.RenderToTexture.hlsl -Fo Assets/Shaders/RenderToTextureVS.dxil
%~dp0/Tools/dxc_2022_12_16/bin/x64/dxc.exe -T ps_6_0 -E PSMain Assets/Shaders/7.RenderToTexture.hlsl -Fo Assets/Shaders/RenderToTexturePS.dxil

%~dp0/Tools/dxc_2022_12_16/bin/x64/dxc.exe -T vs_6_0 -E VSMain Assets/Shaders/7.RenderToTexture.hlsl -Fo Assets/Shaders/LandAndOcean.dxil
%~dp0/Tools/dxc_2022_12_16/bin/x64/dxc.exe -T ps_6_0 -E PSMain Assets/Shaders/7.RenderToTexture.hlsl -Fo Assets/Shaders/LandAndOcean.dxil

%~dp0/Tools/dxc_2022_12_16/bin/x64/dxc.exe -T vs_6_0 -E VSMain Assets/Shaders/7.Skybox.hlsl -Fo Assets/Shaders/SkyboxVS.dxil
%~dp0/Tools/dxc_2022_12_16/bin/x64/dxc.exe -T ps_6_0 -E PSMain Assets/Shaders/7.Skybox.hlsl -Fo Assets/Shaders/SkyboxPS.dxil