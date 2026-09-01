using System.IO;
using UnrealBuildTool;
using System.Collections.Generic;

public class ProtobufCore : ModuleRules
{
    public ProtobufCore(ReadOnlyTargetRules Target) : base(Target)
    {
        Type = ModuleType.External;

        string ThirdPartyPath = ModuleDirectory;
        PublicIncludePaths.Add(Path.Combine(ThirdPartyPath, "Include"));
        
        string LibPath = Path.Combine(ThirdPartyPath, "Lib", "Win64");
        
        if (Directory.Exists(LibPath))
        {
            string[] LibFiles = Directory.GetFiles(LibPath, "*.lib");
            foreach (string lib in LibFiles)
            {
                PublicAdditionalLibraries.Add(lib);
            }
        }
    }
}
