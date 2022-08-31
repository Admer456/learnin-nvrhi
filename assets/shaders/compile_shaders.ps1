
## I installed DXC into D:/dxc
## Yours might be in a different location
$DXC = "D:/dxc/bin/x64/dxc.exe"
$FXC = "D:/DXSDK_June2010/Utilities/bin/x64/fxc.exe"

.\nvrhi-scomp.exe -c $DXC -i shaders.cfg -o "vk/" -P SPIRV -D SPIRV -f
.\nvrhi-scomp.exe -c $DXC -i shaders.cfg -o "dx12/" -P DXIL -D DXIL -f
.\nvrhi-scomp.exe -c $FXC -i shaders.cfg -o "dx11/" -P DXBC -D DXBC -f
