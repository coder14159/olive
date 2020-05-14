##
## Auto Generated makefile by CodeLite IDE
## any manual changes will be erased      
##
## Debug
ProjectName            :=lib
ConfigurationName      :=Debug
WorkspacePath          :=C:/cygwin64/home/lan/dev/git/spmc
ProjectPath            :=C:/cygwin64/home/lan/dev/git/spmc/src
IntermediateDirectory  :=./Debug
OutDir                 := $(IntermediateDirectory)
CurrentFileName        :=
CurrentFilePath        :=
CurrentFileFullPath    :=
User                   :=lan
Date                   :=19/03/2020
CodeLitePath           :="C:/Program Files/CodeLite"
LinkerName             :=C:/cygwin64/bin/x86_64-pc-cygwin-g++.exe
SharedObjectLinkerName :=C:/cygwin64/bin/x86_64-pc-cygwin-g++.exe -shared -fPIC
ObjectSuffix           :=.o
DependSuffix           :=.o.d
PreprocessSuffix       :=.i
DebugSwitch            :=-g 
IncludeSwitch          :=-I
LibrarySwitch          :=-l
OutputSwitch           :=-o 
LibraryPathSwitch      :=-L
PreprocessorSwitch     :=-D
SourceSwitch           :=-c 
OutputFile             :=$(IntermediateDirectory)/$(ProjectName).dll
Preprocessors          :=
ObjectSwitch           :=-o 
ArchiveOutputSwitch    := 
PreprocessOnlySwitch   :=-E
ObjectsFileList        :="lib.txt"
PCHCompileFlags        :=
MakeDirCommand         :=makedir
RcCmpOptions           := 
RcCompilerName         :=windres
LinkOptions            :=  
IncludePath            :=  $(IncludeSwitch). $(IncludeSwitch). 
IncludePCH             := 
RcIncludePath          := 
Libs                   := 
ArLibs                 :=  
LibPath                := $(LibraryPathSwitch). 

##
## Common variables
## AR, CXX, CC, AS, CXXFLAGS and CFLAGS can be overriden using an environment variables
##
AR       := C:/cygwin64/bin/x86_64-pc-cygwin-ar.exe rcu
CXX      := C:/cygwin64/bin/x86_64-pc-cygwin-g++.exe
CC       := C:/cygwin64/bin/x86_64-pc-cygwin-gcc.exe
CXXFLAGS :=  -g $(Preprocessors)
CFLAGS   :=  -g $(Preprocessors)
ASFLAGS  := 
AS       := C:/cygwin64/bin/x86_64-pc-cygwin-as.exe


##
## User defined environment variables
##
CodeLiteDir:=C:\Program Files\CodeLite
Srcs=detail/SPMCQueue.cpp detail/SharedMemoryCounter.cpp SPMCSink.cpp Throughput.cpp TimeDuration.cpp Logger.cpp LatencyStats.cpp PerformanceStats.cpp Latency.cpp Time.cpp \
	SPMCSinks.cpp CpuBind.cpp SPMCStream.cpp ThroughputStats.cpp 

Objects0=$(IntermediateDirectory)/detail_SPMCQueue.cpp$(ObjectSuffix) $(IntermediateDirectory)/detail_SharedMemoryCounter.cpp$(ObjectSuffix) $(IntermediateDirectory)/SPMCSink.cpp$(ObjectSuffix) $(IntermediateDirectory)/Throughput.cpp$(ObjectSuffix) $(IntermediateDirectory)/TimeDuration.cpp$(ObjectSuffix) $(IntermediateDirectory)/Logger.cpp$(ObjectSuffix) $(IntermediateDirectory)/LatencyStats.cpp$(ObjectSuffix) $(IntermediateDirectory)/PerformanceStats.cpp$(ObjectSuffix) $(IntermediateDirectory)/Latency.cpp$(ObjectSuffix) $(IntermediateDirectory)/Time.cpp$(ObjectSuffix) \
	$(IntermediateDirectory)/SPMCSinks.cpp$(ObjectSuffix) $(IntermediateDirectory)/CpuBind.cpp$(ObjectSuffix) $(IntermediateDirectory)/SPMCStream.cpp$(ObjectSuffix) $(IntermediateDirectory)/ThroughputStats.cpp$(ObjectSuffix) 



Objects=$(Objects0) 

##
## Main Build Targets 
##
.PHONY: all clean PreBuild PrePreBuild PostBuild MakeIntermediateDirs
all: $(OutputFile)

$(OutputFile): $(IntermediateDirectory)/.d $(Objects) 
	@$(MakeDirCommand) $(@D)
	@echo "" > $(IntermediateDirectory)/.d
	@echo $(Objects0)  > $(ObjectsFileList)
	$(SharedObjectLinkerName) $(OutputSwitch)$(OutputFile) @$(ObjectsFileList) $(LibPath) $(Libs) $(LinkOptions)
	@$(MakeDirCommand) "C:\cygwin64\home\lan\dev\git\spmc/.build-debug"
	@echo rebuilt > "C:\cygwin64\home\lan\dev\git\spmc/.build-debug/lib"

MakeIntermediateDirs:
	@$(MakeDirCommand) "./Debug"


$(IntermediateDirectory)/.d:
	@$(MakeDirCommand) "./Debug"

PreBuild:
##
## Clean
##
clean:
	$(RM) -r ./Debug/


