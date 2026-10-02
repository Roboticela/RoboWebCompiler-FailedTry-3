CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -O2 -ISource
OBJDIR := Build/Objects

RUNTIME_OBJS := \
	$(OBJDIR)/Document.o \
	$(OBJDIR)/ElementCatalog.o \
	$(OBJDIR)/LayoutEngine.o \
	$(OBJDIR)/InterFontData.o \
	$(OBJDIR)/BitmapFont.o \
	$(OBJDIR)/ImageDecoder.o \
	$(OBJDIR)/Renderer.o \
	$(OBJDIR)/StyleSheet.o \
	$(OBJDIR)/Interaction.o \
	$(OBJDIR)/HostLoop.o \
	$(OBJDIR)/Win32Host.o

COMPILER_OBJS := \
	$(OBJDIR)/Diagnostics.o \
	$(OBJDIR)/ElementCatalog.o \
	$(OBJDIR)/AttributeCatalog.o \
	$(OBJDIR)/TreeValidator.o \
	$(OBJDIR)/CharacterReference.o \
	$(OBJDIR)/HtmlParser.o \
	$(OBJDIR)/Document.o \
	$(OBJDIR)/CodeGenerator.o \
	$(OBJDIR)/CompilerMain.o

TEST_OBJS := \
	$(OBJDIR)/Diagnostics.o \
	$(OBJDIR)/ElementCatalog.o \
	$(OBJDIR)/AttributeCatalog.o \
	$(OBJDIR)/TreeValidator.o \
	$(OBJDIR)/CharacterReference.o \
	$(OBJDIR)/HtmlParser.o \
	$(OBJDIR)/Document.o \
	$(OBJDIR)/LayoutEngine.o \
	$(OBJDIR)/InterFontData.o \
	$(OBJDIR)/BitmapFont.o \
	$(OBJDIR)/ImageDecoder.o \
	$(OBJDIR)/Renderer.o \
	$(OBJDIR)/StyleSheet.o \
	$(OBJDIR)/Interaction.o \
	$(OBJDIR)/HostLoop.o \
	$(OBJDIR)/Win32Host.o \
	$(OBJDIR)/CodeGenerator.o \
	$(OBJDIR)/TestMain.o

.PHONY: all test clean

all: Build/RoboWebCompiler.exe Build/RoboWebTests.exe test

test: Build/RoboWebTests.exe
	Build\RoboWebTests.exe

Build/RoboWebCompiler.exe: $(COMPILER_OBJS)
	$(CXX) $(CXXFLAGS) $(COMPILER_OBJS) -o $@

Build/RoboWebTests.exe: $(TEST_OBJS)
	$(CXX) $(CXXFLAGS) $(TEST_OBJS) -o $@ -lgdi32 -luser32 -lcomdlg32 -lwininet

$(OBJDIR):
	if not exist Build\Objects mkdir Build\Objects

$(OBJDIR)/Diagnostics.o: Source/Diagnostics/Diagnostics.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/ElementCatalog.o: Source/Catalog/ElementCatalog.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/AttributeCatalog.o: Source/Catalog/AttributeCatalog.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/TreeValidator.o: Source/Validate/TreeValidator.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/CharacterReference.o: Source/Parser/CharacterReference.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/HtmlParser.o: Source/Parser/HtmlParser.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/Document.o: Source/Dom/Document.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/LayoutEngine.o: Source/Layout/LayoutEngine.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/InterFontData.o: Source/Render/InterFontData.cpp | $(OBJDIR)
	$(CXX) -std=c++17 -Wall -Wextra -Wpedantic -O0 -ISource -c $< -o $@

$(OBJDIR)/BitmapFont.o: Source/Render/BitmapFont.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/ImageDecoder.o: Source/Render/ImageDecoder.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/Renderer.o: Source/Render/Renderer.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/StyleSheet.o: Source/Render/StyleSheet.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/Interaction.o: Source/Runtime/Interaction.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/HostLoop.o: Source/Runtime/HostLoop.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/Win32Host.o: Source/Runtime/Win32Host.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/CodeGenerator.o: Source/Compiler/CodeGenerator.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/CompilerMain.o: Source/Compiler/CompilerMain.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJDIR)/TestMain.o: Source/Tests/TestMain.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	if exist Build\Objects rmdir /S /Q Build\Objects
	if exist Build\RoboWebCompiler.exe del /Q Build\RoboWebCompiler.exe
	if exist Build\RoboWebTests.exe del /Q Build\RoboWebTests.exe
