
## I installed DXC into D:/dxc
## Yours might be in a different location
$DXC = "D:/dxc/bin/x64/dxc.exe"

.\nvrhi-scomp.exe -c $DXC -i shaders.cfg -o "vk/" -P SPIRV -D SPIRV
.\nvrhi-scomp.exe -c $DXC -i shaders.cfg -o "dx12/" -P DXIL -D DXIL
.\nvrhi-scomp.exe -c $DXC -i shaders.cfg -o "dx11/" -P DXBC -D DXBC
