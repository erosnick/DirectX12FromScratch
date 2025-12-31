dxc.exe textureCube.hlsl -Zpr -T vs_6_0 -E VSMain -Fo textureCube_VS.dxil
dxc.exe textureCube.hlsl -T ps_6_0 -E PSMain -Fo textureCube_PS.dxil
