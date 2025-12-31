dxc.exe quad.hlsl -Zpr -T vs_6_6 -E VSMain -Fo quad_VS.dxil -Zi -Qembed_debug
dxc.exe quad.hlsl -T ps_6_6 -E PSMain -Fo quad_PS.dxil -Zi -Qembed_debug
dxc.exe skybox.hlsl -Zpr -T vs_6_6 -E VSMain -Fo skybox_VS.dxil -Zi -Qembed_debug
dxc.exe skybox.hlsl -T ps_6_6 -E PSMain -Fo skybox_PS.dxil -Zi -Qembed_debug
dxc.exe textureCubeDxc.hlsl -Zpr -T vs_6_6 -E VSMain -Fo textureCubeDxc_VS.dxil -Zi -Qembed_debug
dxc.exe textureCubeDxc.hlsl -T ps_6_6 -E PSMain -Fo textureCubeDxc_PS.dxil -Zi -Qembed_debug
