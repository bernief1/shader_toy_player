// ====================
// shadertoy_player.cpp
// ====================

// ======================================================================================================================================
// TODO --
// + decouple buffers from pass outputs - allow for say pass 0 to render to an MRT and pass 1 to render to just one layer of the same MRT
// + allow MRTs with different formats
// - framebuffer blending (do we really need this? since we can bind the output as an input ..)
// - cubemap and volume textures
// + buffers can be texture arrays (different from MRT as you can bind the whole array for sampling)
// + integer formats (need isampler, usampler etc.)
// - automatic mipmap generation
// - subpasses (each pass renders N times, N controllable in pass metadata, and a shader uniform int 'iPass' is available)
// - mouse passive motion and wheel support
// + imageLoadStore
// - allow hookup between bool SLIDER_VAR and keyboard toggles
// + extended keyboard input texture: store uint16 instead of toggle, ...
// - would be nice to have a running graph interface - e.g. number of visible lightmap texels over time
//    need image to store array of float data
//    every frame, the image writes to itself one row down and one row over
// + remove indices from inputs and images (still need them for outputs i guess)
// + allow for passes to be references from COMMON.glsl
// - improve shader compiling - allow for recompiling, allow for #defines to be referenced from working directory, etc.

#define SUPPORT_IMAGES (1)

// ======================================================================================================================================

#include "common/common.h"

#include "GraphicsTools/util/camera.h"
#include "GraphicsTools/util/crc.h"
#include "GraphicsTools/util/fileutil.h"
#include "GraphicsTools/util/font.h"
#include "GraphicsTools/util/framebuffer.h"
#include "GraphicsTools/util/gui.h"
#include "GraphicsTools/util/imagedxt.h"
#include "GraphicsTools/util/imageutil.h"
#include "GraphicsTools/util/keyboard.h"
#include "GraphicsTools/util/memory.h"
#include "GraphicsTools/util/mesh.h"
#include "GraphicsTools/util/progressdisplay.h"
#include "GraphicsTools/util/stringutil.h"

#include "vmath/vmath_color.h"
#include "vmath/vmath_float16.h"
#include "vmath/vmath_matrix.h"
#include "vmath/vmath_random.h"
#include "vmath/vmath_sampling.h"
#include "vmath/vmath_sphere.h"
#include "vmath/vmath_test.h"
#include "vmath/vmath_triangle.h"
#include "vmath/vmath_vec4.h"

#include <thread>
#include <random>

#include "shaders_common/shadertoy_common.h"

static std::string GetOpenGLTextureTargetStr(GLenum target)
{
	switch (target) {
	case GL_TEXTURE_1D:                   return "GL_TEXTURE_1D";
	case GL_TEXTURE_1D_ARRAY:             return "GL_TEXTURE_1D_ARRAY";
	case GL_TEXTURE_2D:                   return "GL_TEXTURE_2D";
	case GL_TEXTURE_2D_ARRAY:             return "GL_TEXTURE_2D_ARRAY";
	case GL_TEXTURE_2D_MULTISAMPLE:       return "GL_TEXTURE_2D_MULTISAMPLE";
	case GL_TEXTURE_2D_MULTISAMPLE_ARRAY: return "GL_TEXTURE_2D_MULTISAMPLE_ARRAY";
	case GL_TEXTURE_3D:                   return "GL_TEXTURE_3D";
	case GL_TEXTURE_CUBE_MAP:             return "GL_TEXTURE_CUBE_MAP";
	case GL_TEXTURE_CUBE_MAP_ARRAY:       return "GL_TEXTURE_CUBE_MAP_ARRAY";
	case GL_TEXTURE_RECTANGLE:            return "GL_TEXTURE_RECTANGLE";
	case GL_TEXTURE_BUFFER:               return "GL_TEXTURE_BUFFER";
	case GL_NONE:                         return "GL_NONE";
	}
	return varString("UNKNOWN(%i)", target);
}

static GLenum GetOpenGLTextureTargetBinding(GLenum target)
{
	switch (target) {
	case GL_TEXTURE_1D:                   return GL_TEXTURE_BINDING_1D;
	case GL_TEXTURE_1D_ARRAY:             return GL_TEXTURE_BINDING_1D_ARRAY;
	case GL_TEXTURE_2D:                   return GL_TEXTURE_BINDING_2D;
	case GL_TEXTURE_2D_ARRAY:             return GL_TEXTURE_BINDING_2D_ARRAY;
	case GL_TEXTURE_2D_MULTISAMPLE:       return GL_TEXTURE_BINDING_2D_MULTISAMPLE;
	case GL_TEXTURE_2D_MULTISAMPLE_ARRAY: return GL_TEXTURE_BINDING_2D_MULTISAMPLE_ARRAY;
	case GL_TEXTURE_3D:                   return GL_TEXTURE_BINDING_3D;
	case GL_TEXTURE_CUBE_MAP:             return GL_TEXTURE_BINDING_CUBE_MAP;
	case GL_TEXTURE_CUBE_MAP_ARRAY:       return GL_TEXTURE_BINDING_CUBE_MAP_ARRAY;
	case GL_TEXTURE_RECTANGLE:            return GL_TEXTURE_BINDING_RECTANGLE;
	case GL_TEXTURE_BUFFER:               return GL_TEXTURE_BINDING_BUFFER;
	}
	return GL_NONE;
}

static void glBindTextureDEBUG(GLenum target, GLuint textureID)
{
#if 0
	GLuint textureID_prev = 0;
	glGetIntegerv(GetOpenGLTextureTargetBinding(target), (GLint*)&textureID_prev);
	printf("*** binding target %s to textureID %u (was bound to texture ID %u)\n", GetOpenGLTextureTargetStr(target).c_str(), textureID, textureID_prev);
	const GLenum targets[] = {
		GL_TEXTURE_1D,
		GL_TEXTURE_1D_ARRAY,
		GL_TEXTURE_2D,
		GL_TEXTURE_2D_ARRAY,
		GL_TEXTURE_2D_MULTISAMPLE,
		GL_TEXTURE_2D_MULTISAMPLE_ARRAY,
		GL_TEXTURE_3D,
		GL_TEXTURE_CUBE_MAP,
		GL_TEXTURE_CUBE_MAP_ARRAY,
		GL_TEXTURE_RECTANGLE,
		GL_TEXTURE_BUFFER,
	};
	for (int i = 0; i < icountof(targets); i++) {
		glGetIntegerv(GetOpenGLTextureTargetBinding(targets[i]), (GLint*)&textureID_prev);
		printf("***   texture ID %u was bound by target %s\n", textureID_prev, GetOpenGLTextureTargetStr(targets[i]).c_str());
	}
#endif
	glBindTexture(target, textureID);
}

static uint32 g_ViewportWidth = 960;
static uint32 g_ViewportHeight = 512;
static std::string g_ShadersDir = "shaders";
static bool g_GPUShader5 = false;
static Keyboard g_Keyboard;
static int g_MouseDragCurr[2] = {0,0};
static int g_MouseDragStart[2] = {0,0};
static bool g_MouseButtonState[3] = {false,false,false}; // left,right,middle
static int g_MouseWheelState = 0; // instantaneous state

#define SHADER_DEFINE_STRING "#define " // note: line must start literally with "#define ", not "<spaces or tabs> # <spaces or tabs> define" etc.
#define SHADER_CODE_MAX_LINE_SIZE 4096

#if USE_GUI
static bool g_GUIEnabled = true;
static GUIFrame* g_GUIFrame = nullptr;
static bool g_GUISliderChanged = false;

static void SetGUISliderChanged(GUISliderElement*, GUI::eGUIEvent)
{
	g_GUISliderChanged = true;
}

enum eGUISliderType
{
	GUI_SLIDER_TYPE_NONE = 0,
	GUI_SLIDER_TYPE_FLOAT,
	GUI_SLIDER_TYPE_INT,
	GUI_SLIDER_TYPE_UINT,
	GUI_SLIDER_TYPE_BOOL,
};

class GUISlider
{
private:
	GUISlider(int passIndex, eGUISliderType type, uint32 components, const char* name, const char* init)
		: m_passIndex(passIndex)
		, m_type(type)
		, m_components(components)
		, m_name(name)
		, m_data(nullptr)
	{
		switch (type) {
		case GUI_SLIDER_TYPE_FLOAT: {
			m_data = new float[components];
			if (init) {
				const float dataInit = (float)atof(init);
				for (uint32 i = 0; i < components; i++)
					((float*)m_data)[i] = dataInit;
			} else
				memset(m_data, 0, components*sizeof(float));
			break;
		} case GUI_SLIDER_TYPE_INT:
			m_data = new int[components];
			if (init) {
				const int dataInit = atoi(init);
				for (uint32 i = 0; i < components; i++)
					((int*)m_data)[i] = dataInit;
			} else
				memset(m_data, 0, components*sizeof(int));
			break;
		case GUI_SLIDER_TYPE_UINT:
			m_data = new uint32[components];
			if (init) {
				const uint32 dataInit = (uint32)atoi(init);
				for (uint32 i = 0; i < components; i++)
					((uint32*)m_data)[i] = dataInit;
			} else
				memset(m_data, 0, components*sizeof(int));
			break;
		case GUI_SLIDER_TYPE_BOOL:
			m_data = new bool[components];
			if (init) {
				const bool dataInit = stricmp(init, "true") == 0;
				for (uint32 i = 0; i < components; i++)
					((bool*)m_data)[i] = dataInit;
			} else
				memset(m_data, 0, components*sizeof(bool));
			break;
		};
	}

public:
	static void SetCurrentPassIndexForShaderLoad(int passIndex)
	{
		GetCurrentPassIndexForShaderLoad() = passIndex;
	}

	static bool AddSlider(const char* shaderPath, uint32 shaderLineIndex, const char* line)
	{
		// e.g. "SLIDER_VAR(vec3,myvec,1,-2,2);"
		while (*line == ' ' || *line == '\t')
			line++;
		if (if_strskip(line, "SLIDER_VAR(")) {
			char temp[SHADER_CODE_MAX_LINE_SIZE];
			strcpy(temp, line);
			char* end = strchr(temp, ')');
			if (end) {
				*end = '\0';
				NameValuePairs params(temp);
				if (params.size() >= 3) { // (type,name,init) are mandatory
					const char* typeStr = params[0].m_value.c_str();
					const char* nameStr = params[1].m_value.c_str();
					const char* initStr = params[2].m_value.c_str();
					eGUISliderType type = GUI_SLIDER_TYPE_NONE;
					uint32 components = 0;
					if      (strcmp(typeStr, "float") == 0) { type = GUI_SLIDER_TYPE_FLOAT; components = 1; }
					else if (strcmp(typeStr, "vec2" ) == 0) { type = GUI_SLIDER_TYPE_FLOAT; components = 2; }
					else if (strcmp(typeStr, "vec3" ) == 0) { type = GUI_SLIDER_TYPE_FLOAT; components = 3; }
					else if (strcmp(typeStr, "vec4" ) == 0) { type = GUI_SLIDER_TYPE_FLOAT; components = 4; }
					if      (strcmp(typeStr, "int"  ) == 0) { type = GUI_SLIDER_TYPE_INT;   components = 1; }
					else if (strcmp(typeStr, "ivec2") == 0) { type = GUI_SLIDER_TYPE_INT;   components = 2; }
					else if (strcmp(typeStr, "ivec3") == 0) { type = GUI_SLIDER_TYPE_INT;   components = 3; }
					else if (strcmp(typeStr, "ivec4") == 0) { type = GUI_SLIDER_TYPE_INT;   components = 4; }
					if      (strcmp(typeStr, "uint" ) == 0) { type = GUI_SLIDER_TYPE_UINT;  components = 1; }
					else if (strcmp(typeStr, "uvec2") == 0) { type = GUI_SLIDER_TYPE_UINT;  components = 2; }
					else if (strcmp(typeStr, "uvec3") == 0) { type = GUI_SLIDER_TYPE_UINT;  components = 3; }
					else if (strcmp(typeStr, "uvec4") == 0) { type = GUI_SLIDER_TYPE_UINT;  components = 4; }
					if      (strcmp(typeStr, "bool" ) == 0) { type = GUI_SLIDER_TYPE_BOOL;  components = 1; }
					else if (strcmp(typeStr, "bvec2") == 0) { type = GUI_SLIDER_TYPE_BOOL;  components = 2; }
					else if (strcmp(typeStr, "bvec3") == 0) { type = GUI_SLIDER_TYPE_BOOL;  components = 3; }
					else if (strcmp(typeStr, "bvec4") == 0) { type = GUI_SLIDER_TYPE_BOOL;  components = 4; }
					if (type != GUI_SLIDER_TYPE_NONE || (type != GUI_SLIDER_TYPE_BOOL && params.size() < 5)) {
						const char* minStr = params.size() > 3 ? params[3].m_value.c_str() : nullptr;
						const char* maxStr = params.size() > 4 ? params[4].m_value.c_str() : nullptr;
						const int passIndex = GetCurrentPassIndexForShaderLoad();
						GUISlider* slider = new GUISlider(passIndex, type, components, nameStr, initStr);
						if (g_GUIFrame == nullptr) {
							GUIWindow* window = new GUIWindow();
							g_GUIFrame = &window->m_frame;
							GUI::RegisterWindow(window);
						}
						for (uint32 i = 0; i < components; i++) {
							char componentName[256] = "";
							if (passIndex != -1)
								sprintf(componentName, "pass %i - ", passIndex);
							strcat(componentName, slider->m_name.c_str());
							if (components > 1)
								strcatf(componentName, ".%c", 'x' + i);
							for (char* s = componentName; *s; s++)
								if (*s == '_')
									*s = ' ';
							switch (type) {
							case GUI_SLIDER_TYPE_FLOAT:
								g_GUIFrame->AddElement(new GUISliderElement(componentName, ((float*)slider->m_data)[i], (float)atof(minStr), (float)atof(maxStr), SetGUISliderChanged));
								break;
							case GUI_SLIDER_TYPE_INT:
								g_GUIFrame->AddElement(new GUIIntSliderElement(componentName, ((int*)slider->m_data)[i], atoi(minStr), atoi(maxStr), SetGUISliderChanged));
								break;
							case GUI_SLIDER_TYPE_UINT:
								g_GUIFrame->AddElement(new GUIIntSliderElement(componentName, ((int*)slider->m_data)[i], atoi(minStr), atoi(maxStr), SetGUISliderChanged)); // TODO -- uint sliders
								break;
							case GUI_SLIDER_TYPE_BOOL:
								g_GUIFrame->AddElement(new GUIBoolSliderElement(componentName, ((bool*)slider->m_data)[i], SetGUISliderChanged));
								break;
							}
						}
						GetSliders().push_back(slider);
						g_GUIFrame->AlignSliders();
						return true;
					} else
						fprintf(stderr, "shader error in %s (line %u): unknown type for SLIDER_VAR\n", shaderPath, shaderLineIndex);
				} else
					fprintf(stderr, "shader error in %s (line %u): incorrect number of params for SLIDER_VAR\n", shaderPath, shaderLineIndex);
			} else
				fprintf(stderr, "shader error in %s (line %u): no close parenthesis found for SLIDER_VAR\n", shaderPath, shaderLineIndex);
		}
		return false;
	}

	static void SetUniformsForPass(uint32 passIndex, GLuint programID)
	{
		const std::vector<GUISlider*>& sliders = GetSliders();
		for (uint32 i = 0; i < sliders.size(); i++) {
			const GUISlider* slider = sliders[i];
			if (slider->m_passIndex == -1 || slider->m_passIndex == (int)passIndex) {
				switch (slider->m_type) {
				case GUI_SLIDER_TYPE_FLOAT: {
					const float* data = (const float*)slider->m_data;
					switch (slider->m_components) {
					case 1: glUniform1f(glGetUniformLocation(programID, slider->m_name.c_str()), data[0]); break;
					case 2: glUniform2f(glGetUniformLocation(programID, slider->m_name.c_str()), data[0], data[1]); break;
					case 3: glUniform3f(glGetUniformLocation(programID, slider->m_name.c_str()), data[0], data[1], data[2]); break;
					case 4: glUniform4f(glGetUniformLocation(programID, slider->m_name.c_str()), data[0], data[1], data[2], data[3]); break;
					}
					break;
				}
				case GUI_SLIDER_TYPE_INT: {
					const int* data = (const int*)slider->m_data;
					switch (slider->m_components) {
					case 1: glUniform1i(glGetUniformLocation(programID, slider->m_name.c_str()), data[0]); break;
					case 2: glUniform2i(glGetUniformLocation(programID, slider->m_name.c_str()), data[0], data[1]); break;
					case 3: glUniform3i(glGetUniformLocation(programID, slider->m_name.c_str()), data[0], data[1], data[2]); break;
					case 4: glUniform4i(glGetUniformLocation(programID, slider->m_name.c_str()), data[0], data[1], data[2], data[3]); break;
					}
					break;
				}
				case GUI_SLIDER_TYPE_UINT: {
					const uint32* data = (const uint32*)slider->m_data;
					switch (slider->m_components) {
					case 1: glUniform1ui(glGetUniformLocation(programID, slider->m_name.c_str()), data[0]); break;
					case 2: glUniform2ui(glGetUniformLocation(programID, slider->m_name.c_str()), data[0], data[1]); break;
					case 3: glUniform3ui(glGetUniformLocation(programID, slider->m_name.c_str()), data[0], data[1], data[2]); break;
					case 4: glUniform4ui(glGetUniformLocation(programID, slider->m_name.c_str()), data[0], data[1], data[2], data[3]); break;
					}
					break;
				}
				case GUI_SLIDER_TYPE_BOOL: {
					const bool* data = (const bool*)slider->m_data;
					switch (slider->m_components) {
					case 1: glUniform1ui(glGetUniformLocation(programID, slider->m_name.c_str()), data[0]?1:0); break;
					case 2: glUniform2ui(glGetUniformLocation(programID, slider->m_name.c_str()), data[0]?1:0, data[1]?1:0); break;
					case 3: glUniform3ui(glGetUniformLocation(programID, slider->m_name.c_str()), data[0]?1:0, data[1]?1:0, data[2]?1:0); break;
					case 4: glUniform4ui(glGetUniformLocation(programID, slider->m_name.c_str()), data[0]?1:0, data[1]?1:0, data[2]?1:0, data[3]?1:0); break;
					}
					break;
				}
				}
			}
		}
		glUniform1ui(glGetUniformLocation(programID, "g_GUISliderChanged"), g_GUISliderChanged?1:0);
	}

private:
	static std::vector<GUISlider*>& GetSliders()
	{
		static std::vector<GUISlider*> sliders;
		return sliders;
	}

	static int& GetCurrentPassIndexForShaderLoad()
	{
		static int passIndex = -1;
		return passIndex;
	}

	int m_passIndex; // -1=common
	eGUISliderType m_type;
	uint32 m_components; // [1..4]
	std::string m_name;
	void* m_data;
};
#endif // USE_GUI

static uint32 g_NumShaderCompilerLinkErrors = 0;
static const char* g_CurrentShaderBeingCompiled = nullptr;
static std::map<GLenum,std::map<GLuint,std::string> > g_ShaderToProcessedPath; // target -> programID -> path

static void LoadShaderCodeInternal(
	std::string& code,
	const char* path,
	std::map<std::string,bool>& included,
	const std::map<std::string,std::string>* defineOverrides)
{
	FILE* file = fopen(path, "r");
	if (file) {
		char line[SHADER_CODE_MAX_LINE_SIZE];
		char temp[SHADER_CODE_MAX_LINE_SIZE];
		uint32 lineIndex = 0;
		while (fgets(line, sizeof(line), file)) {
			lineIndex++;
			char* end = strpbrk(line, "\r\n");
			if (end)
				*end = '\0';
		#if USE_GUI
			if (g_GUIEnabled)
				GUISlider::AddSlider(path, lineIndex, line);
		#endif // USE_GUI
			char* s = line;
			while (*s == ' ' || *s == '\t')
				s++;
			if (strstr(s, "#include") == s) {
				strcpy(temp, s);
				char* includePathBegin = strchr(temp, '\"');
				if (includePathBegin) {
					*includePathBegin++ = '\0';
					char* includePathEnd = strchr(includePathBegin, '\"');
					if (includePathEnd) {
						*includePathEnd = '\0';
						char includePath[512];
						strcpy(includePath, path);
						s = strrchr(includePath, '\\');
						if (s)
							s[1] = '\0';
						strcat(includePath, includePathBegin);
						if (included.find(includePath) == included.end()) {
							included[includePath] = true;
							code += varString("//<=== BEGIN %s ===>\n", line);
							LoadShaderCodeInternal(code, includePath, included, defineOverrides);
							code += varString("//<=== END %s ===>\n", line);
						}
						continue;
					}
				}
			}
			if (defineOverrides && strstr(s, SHADER_DEFINE_STRING) == s) {
				strcpy(temp, s + strlen(SHADER_DEFINE_STRING));
				const char* d = strtok(temp, " \t");
				const auto it = defineOverrides->find(d);
				if (it != defineOverrides->end() && !it->second.empty()) {
					code += varString("%s%s %s\n", SHADER_DEFINE_STRING, d, it->second.c_str());
					continue;
				}
			}
			code += varString("%s\n", line);
		}
		fclose(file);
	}
}

static bool LoadShader(
	GLuint& shaderID,
	const char* path,
	const char* processedPathExt,
	GLenum target,
	const char* versionStr,
	const std::map<std::string,std::string>* defineOverrides = nullptr,
	const std::vector<std::string>* sourceHeader = nullptr,
	const std::vector<std::string>* sourceFooter = nullptr)
{
	std::map<std::string,bool> included;
	std::string code;
	if (versionStr)
		code += varString("%s\n", versionStr);
	if (g_GPUShader5 && target == GL_FRAGMENT_SHADER) {
		code += "#extension GL_NV_gpu_shader5 : enable\n";
		code += "#extension GL_NV_bindless_texture : enable\n"; // just so i can pass samplers around as uint64_t's ..
		code += "#extension GL_ARB_derivative_control : enable\n";
		code += "#define _GPU_SHADER_5_\n";
	}
	code += std::string("#define _SHADERTOY_PLAYER_VERSION_ 1\n");
	if (defineOverrides) {
		code += std::string("//<=== BEGIN DEFINES ===>\n");
		for (auto iter = defineOverrides->begin(); iter != defineOverrides->end(); ++iter)
			if (iter->second.empty())
				code += varString("%s%s\n", SHADER_DEFINE_STRING, iter->first.c_str());
		code += std::string("//<=== END DEFINES ===>\n");
	}
	if (sourceHeader) {
		for (uint32 i = 0; i < sourceHeader->size(); i++) {
			code += sourceHeader->operator[](i);
			code += "\n";
		}
	}
	LoadShaderCodeInternal(code, path, included, defineOverrides);
	if (sourceFooter) {
		for (uint32 i = 0; i < sourceFooter->size(); i++) {
			code += sourceFooter->operator[](i);
			code += "\n";
		}
	}
	if (shaderID == 0)
		shaderID = glCreateShader(target);
	char processedPath[512] = "";
	if (processedPathExt)
		ForceAssert(processedPathExt[0] == '_'); // should start with underscore
	const char* ext = ".glsl";
	switch (target) { // glslangValidator compatibility
	case GL_VERTEX_SHADER         : ext = ".vert"; break;
	case GL_TESS_CONTROL_SHADER   : ext = ".tesc"; break;
	case GL_TESS_EVALUATION_SHADER: ext = ".tese"; break;
	case GL_GEOMETRY_SHADER       : ext = ".geom"; break;
	case GL_FRAGMENT_SHADER       : ext = ".frag"; break;
	case GL_COMPUTE_SHADER        : ext = ".comp"; break;
	}
	PathInsertDirectory(processedPath, "_processed", PathExt(path, "_processed%s%s", processedPathExt ? processedPathExt : "", ext));
	FILE* file = fopen(processedPath, "w");
	if (file) {
		fprintf(file, "%s", code.c_str());
		fclose(file);
	}
	ForceAssert(g_ShaderToProcessedPath[target].find(shaderID) == g_ShaderToProcessedPath[target].end()); // make sure we don't collide
	g_ShaderToProcessedPath[target][shaderID] = processedPath;
	const char* codeStr = code.c_str();
	g_CurrentShaderBeingCompiled = processedPath;
	glShaderSource(shaderID, 1, (const GLcharARB**)&codeStr, nullptr);
	glCompileShader(shaderID);
	g_CurrentShaderBeingCompiled = nullptr;
	GLint compileStatus = 0;
	glGetShaderiv(shaderID, GL_COMPILE_STATUS, &compileStatus);
	if (compileStatus == GL_TRUE)
		return true;
	else {
		GLint maxLength = 0;
		glGetShaderiv(shaderID, GL_INFO_LOG_LENGTH, &maxLength);
		std::vector<GLchar> infoLog(maxLength);
		glGetShaderInfoLog(shaderID, maxLength, &maxLength, infoLog.data());
		fprintf(stderr, "compile (%s): %s\n", processedPath, infoLog.data());
		if (1) { // insert compile error into processed shader text and open it ..
			const char* s = strchr(infoLog.data(), '(');
			if (s) {
				const uint32 errorLineIndex = atoi(s + 1);
				FILE* f1 = fopen(processedPath, "r");
				if (f1) {
					char processedPath2[512];
					strcpy(processedPath2, PathExt(processedPath, "_error.txt"));
					FILE* f2 = fopen(processedPath2, "w");
					if (f2) {
						uint32 lineIndex = 0;
						char line[SHADER_CODE_MAX_LINE_SIZE];
						bool lineFound = false;
						while (fgets(line, sizeof(line), f1)) {
							fprintf(f2, "%s", line);
							if (++lineIndex == errorLineIndex) {
								fprintf(f2, "%s\n", infoLog.data());
								lineFound = true;
								break; // TODO -- insert multiple errors?
							}
						}
						if (!lineFound) {
							fprintf(f2, "\n\nCOULD NOT FIND LINE %d TO INSERT ERROR!\n", errorLineIndex);
							fprintf(f2, "%s\n", infoLog.data());
						}
						fclose(f2);
						system(processedPath2); // open it
					}
					fclose(f1);
				}
			}
		}
		if (++g_NumShaderCompilerLinkErrors < 5)
			system("pause");
		return false;
	}
}

static void AddCommentsToShaderASM(char*& text, bool flowControl = true)
{
	std::vector<std::string> lines;
	char* buf = new char[strlen(text) + 1];
	char* buf1 = buf;
	strcpy(buf, text);
	uint32 maxLen = 0;
	while (true) {
		char* line = strtok(buf1, "\r\n");
		if (line) {
			lines.push_back(line);
			maxLen = Max((uint32)strlen(line), maxLen);
			buf1 = nullptr;
		} else
			break;
	}
	maxLen += (uint32)varString("[line_%05u] ", 0).size();
	delete[] text; // text has now been copied into lines	
	class FlowControlStackElement
	{
	public:
		FlowControlStackElement(int lineIf, bool isRep) : m_lineNum_IF(lineIf), m_lineNum_ELSE(-1), m_isRep(isRep) {}
		int m_lineNum_IF;
		int m_lineNum_ELSE;
		bool m_isRep;
	};
	std::vector<FlowControlStackElement> flow;
	for (uint32 i = 0; i < lines.size(); i++) {
		const int lineNum = i + 1;
		const char* line = lines[i].c_str();
		std::string line2 = varString("[line_%05u] %s", lineNum, line);
		while (line2.size() < maxLen)
			line2 += " ";
		line2 += " #";
		if (flowControl) { // add relevant line numbers for program flow instructions (IF,ELSE,ENDIF,REP,ENDREP,BRK)
			if (strstartswith(line, "IF ")) {
				flow.push_back(FlowControlStackElement(lineNum, false));
			} else if (strstartswith(line, "ELSE;")) {
				ForceAssert(!flow.empty());
				ForceAssert(!flow.back().m_isRep);
				ForceAssert(flow.back().m_lineNum_ELSE == -1);
				flow.back().m_lineNum_ELSE = lineNum;
			} else if (strstartswith(line, "ENDIF;")) {
				ForceAssert(!flow.empty());
				ForceAssert(!flow.back().m_isRep);
				line2 += varString(" IF:[line_%05u]", (uint32)flow.back().m_lineNum_IF);
				lines[flow.back().m_lineNum_IF - 1] += varString(" IF:[line_%05u]", (uint32)flow.back().m_lineNum_IF); // self
				if (flow.back().m_lineNum_ELSE != -1) {
					line2 += varString(" ELSE:[line_%05u]", (uint32)flow.back().m_lineNum_ELSE);
					lines[flow.back().m_lineNum_IF - 1] += varString(" ELSE:[line_%05u]", (uint32)flow.back().m_lineNum_ELSE);
					lines[flow.back().m_lineNum_ELSE - 1] += varString(" IF:[line_%05u]", (uint32)flow.back().m_lineNum_IF);
					lines[flow.back().m_lineNum_ELSE - 1] += varString(" ELSE:[line_%05u]", (uint32)flow.back().m_lineNum_ELSE); // self
					lines[flow.back().m_lineNum_ELSE - 1] += varString(" ENDIF:[line_%05u]", lineNum);
				}
				lines[flow.back().m_lineNum_IF - 1] += varString(" ENDIF:[line_%05u]", lineNum);
				line2 += varString(" ENDIF:[line_%05u]", lineNum); // self
				flow.pop_back();
			} else if (strstartswith(line, "REP.S ")) {
				flow.push_back(FlowControlStackElement(lineNum, true));
			} else if (strstartswith(line, "ENDREP;")) {
				ForceAssert(!flow.empty());
				ForceAssert(flow.back().m_isRep);
				ForceAssert(flow.back().m_lineNum_ELSE == -1);
				line2 += varString(" REP:[line_%05u]", (uint32)flow.back().m_lineNum_IF);
				lines[flow.back().m_lineNum_IF - 1] += varString(" REP:[line_%05u]", (uint32)flow.back().m_lineNum_IF); // self
				lines[flow.back().m_lineNum_IF - 1] += varString(" ENDREP:[line_%05u]", lineNum);
				line2 += varString(" ENDREP:[line_%05u]", lineNum); // self
				flow.pop_back();
			} else if (strstartswith(line, "BRK ")) {
				ForceAssert(!flow.empty());
				int repIndex = -1;
				for (int j = (int)flow.size() - 1; j >= 0; j--) {
					if (flow[j].m_isRep) {
						repIndex = j;
						break;
					}
				}
				ForceAssert(repIndex != -1);
				line2 += varString(" REP:[line_%05u]", (uint32)flow[repIndex].m_lineNum_IF); // TODO -- show ENDREP line number, also BRK line numbers on REP/ENDREP lines
			}
		}
		lines[i] = line2;
	}
	std::string text2 = "";
	for (uint32 i = 0; i < lines.size(); i++)
		text2 += varString("%s\n", lines[i].c_str());
	text = new char[text2.size() + 1];
	strcpy(text, text2.c_str());
	delete[] buf;
}

static const char* GetShaderProcessedPath(GLuint programID)
{
	if (programID != 0) {
		const auto f = g_ShaderToProcessedPath[GL_SHADER].find(programID);
		if (f != g_ShaderToProcessedPath[GL_SHADER].end())
			return f->second.c_str();
		else
			return "UNKNOWN";
	} else
		return "NONE";
}

static bool CreateShaderProgram(GLuint& programID, GLuint vertexShaderID, GLuint fragmentShaderID, bool dumpASM = true)
{
	if (programID == 0)
		programID = glCreateProgram();
	ForceAssert(vertexShaderID != 0);
	ForceAssert(fragmentShaderID != 0);
	glAttachShader(programID, vertexShaderID);
	glAttachShader(programID, fragmentShaderID);
	if (dumpASM)
		glProgramParameteri(programID, GL_PROGRAM_BINARY_RETRIEVABLE_HINT, GL_TRUE);
	glLinkProgram(programID);
	const char* vsPath = "?";
	const char* fsPath = "?";
	const auto vs = g_ShaderToProcessedPath[GL_VERTEX_SHADER].find(vertexShaderID);
	if (vs != g_ShaderToProcessedPath[GL_VERTEX_SHADER].end()) {
		vsPath = vs->second.c_str();
		const char* slash = strrchr(vsPath, '\\');
		if (slash)
			vsPath = slash + 1;
	}
	const auto fs = g_ShaderToProcessedPath[GL_FRAGMENT_SHADER].find(fragmentShaderID);
	if (fs != g_ShaderToProcessedPath[GL_FRAGMENT_SHADER].end()) {
		fsPath = fs->second.c_str();
		const char* slash = strrchr(fsPath, '\\');
		if (slash)
			fsPath = slash + 1;
	}
	GLint linkStatus = 0;
	glGetProgramiv(programID, GL_LINK_STATUS, &linkStatus);
	if (linkStatus == GL_TRUE) {
		g_ShaderToProcessedPath[GL_SHADER][programID] = varString("(vs=%s, fs=%s)", vsPath, fsPath);
		//fprintf(stderr, "link successful - (vs=%s, fs=%s)\n", vsPath, fsPath);
		if (dumpASM) {
			// http://www.renderguild.com/gpuguide.pdf
			GLint len = 0;
			glGetProgramiv(programID, GL_PROGRAM_BINARY_LENGTH, &len);
			if (len > 0) {
				char* bin = new char[len];
				GLenum format = GL_NONE;
				glGetProgramBinary(programID, len, nullptr, &format, bin);
				FILE* f = fopen(PathExt(fs->second.c_str(), "_asm.txt"), "w");
				if (f) {
					const char* shaderTextStart = "!!NV"; // e.g. "!!NVfp5.0"
					const char* shaderTextEnd = "END\n";
					char* s1 = bin;
					uint32 numShaders = 0;
					while (s1 < bin + len) {
						if (strncmp(s1, shaderTextStart, strlen(shaderTextStart)) == 0) {
							char* s2 = s1 + strlen(shaderTextStart);
							while (s2 < bin + len) {
								if (strncmp(s2, shaderTextEnd, strlen(shaderTextEnd)) == 0) {
									s2 += strlen(shaderTextEnd);
									const size_t shaderTextLength = (ptrdiff_t)(s2 - s1);
									char* temp = new char[shaderTextLength + 1];
									memcpy(temp, s1, shaderTextLength);
									temp[shaderTextLength] = '\0';
									AddCommentsToShaderASM(temp);
									fprintf(f, "%s\n", temp);
									numShaders++;
									s1 = s2;
									delete[] temp;
									break;
								} else
									s2++;
							}
							if (s1 != s2) {
								fprintf(f, "no end found!\n");
								break;
							}
						} else
							s1++;
					}
					bool dumpBinary = true;//false;
					if (numShaders == 0) {
						fprintf(f, "no shaders found!\n");
						fprintf(f, "dumping binary data ..\n\n");
						dumpBinary = true;
					}
					if (dumpBinary) {
						FILE* f2 = fopen(PathExt(fs->second.c_str(), "_asm.bin"), "wb");
						if (f2) {
							fwrite(bin, sizeof(char), len, f2);
							fclose(f2);
						}
					}
					fclose(f);
				}
				delete[] bin;
			}
		}
		return true;
	} else {
		GLint maxLength = 0;
		glGetProgramiv(programID, GL_INFO_LOG_LENGTH, &maxLength);
		std::vector<GLchar> infoLog(maxLength);
		glGetProgramInfoLog(programID, maxLength, &maxLength, &infoLog[0]);
		fprintf(stderr, "link error (vs=%s, fs=%s): %s\n", vsPath, fsPath, infoLog.data());
		if (++g_NumShaderCompilerLinkErrors < 5)
			system("pause");
		return false;
	}
}

#define VERTEX_SHADER_VERSION_STR "#version 440"
#define FRAGMENT_SHADER_VERSION_STR "#version 440"

static GLuint LoadShaderProgram(const char* fragmentShaderPath, GLuint commonVertexShaderID,
	const std::vector<std::string>* fragmentShaderSourceHeader = nullptr,
	const std::vector<std::string>* fragmentShaderSourceFooter = nullptr)
{
	GLuint programID = 0;
	if (FileExists(fragmentShaderPath)) {
		GLuint vertexShaderID = 0;
		char vertexShaderPath[512];
		strcpy(vertexShaderPath, PathExt(fragmentShaderPath, "_vs.glsl"));
		if (FileExists(vertexShaderPath))
			LoadShader(vertexShaderID, vertexShaderPath, nullptr, GL_VERTEX_SHADER, VERTEX_SHADER_VERSION_STR);
		else
			vertexShaderID = commonVertexShaderID;
		if (vertexShaderID != 0) {
			GLuint fragmentShaderID = 0;
			if (LoadShader(fragmentShaderID, fragmentShaderPath, nullptr, GL_FRAGMENT_SHADER, FRAGMENT_SHADER_VERSION_STR, nullptr, fragmentShaderSourceHeader, fragmentShaderSourceFooter))
				CreateShaderProgram(programID, vertexShaderID, fragmentShaderID);
		}
	}
	return programID;
}

class TextureFormatInfo
{
public:
	enum eSamplerType
	{
		SAMPLER_TYPE_FLOAT,        // e.g. 'sampler2D'
		SAMPLER_TYPE_UNSIGNED_INT, // e.g. 'usampler2D'
		SAMPLER_TYPE_SIGNED_INT,   // e.g. 'isampler2D'
	};

	TextureFormatInfo(DDS_DXGI_FORMAT format)
		: m_formatQualifier("unknown")
		, m_formatType("unknown")
		, m_internalFormat(GL_NONE)
		, m_format(GL_NONE)
		, m_type(GL_NONE)
		, m_samplerType(SAMPLER_TYPE_FLOAT)
		, m_components(0)
		, m_compressed(false)
	{
		switch (format) {
		case DDS_DXGI_FORMAT_R8_UNORM:            m_formatQualifier = "r8";             m_internalFormat = GL_R8;             m_format = GL_RED;          m_type = GL_UNSIGNED_BYTE;                m_components = 1; break;
		case DDS_DXGI_FORMAT_R8_SNORM:            m_formatQualifier = "r8_snorm";       m_internalFormat = GL_R8_SNORM;       m_format = GL_RED;          m_type = GL_BYTE;                         m_components = 1; break;
		case DDS_DXGI_FORMAT_R16_UNORM:           m_formatQualifier = "r16";            m_internalFormat = GL_R16;            m_format = GL_RED;          m_type = GL_UNSIGNED_SHORT;               m_components = 1; break;
		case DDS_DXGI_FORMAT_R16_SNORM:           m_formatQualifier = "r16_snorm";      m_internalFormat = GL_R16_SNORM;      m_format = GL_RED;          m_type = GL_SHORT;                        m_components = 1; break;
		case DDS_DXGI_FORMAT_R16_FLOAT:           m_formatQualifier = "r16f";           m_internalFormat = GL_R16F;           m_format = GL_RED;          m_type = GL_HALF_FLOAT;                   m_components = 1; break;
		case DDS_DXGI_FORMAT_R32_FLOAT:           m_formatQualifier = "r32f";           m_internalFormat = GL_R32F;           m_format = GL_RED;          m_type = GL_FLOAT;                        m_components = 1; break;
		case DDS_DXGI_FORMAT_R8G8_UNORM:          m_formatQualifier = "rg8";            m_internalFormat = GL_RG8;            m_format = GL_RG;           m_type = GL_UNSIGNED_BYTE;                m_components = 2; break;
		case DDS_DXGI_FORMAT_R8G8_SNORM:          m_formatQualifier = "rg8_snorm";      m_internalFormat = GL_RG8_SNORM;      m_format = GL_RG;           m_type = GL_BYTE;                         m_components = 2; break;
		case DDS_DXGI_FORMAT_R16G16_UNORM:        m_formatQualifier = "rg16";           m_internalFormat = GL_RG16;           m_format = GL_RG;           m_type = GL_UNSIGNED_SHORT;               m_components = 2; break;
		case DDS_DXGI_FORMAT_R16G16_SNORM:        m_formatQualifier = "rg16_snorm";     m_internalFormat = GL_RG16_SNORM;     m_format = GL_RG;           m_type = GL_SHORT;                        m_components = 2; break;
		case DDS_DXGI_FORMAT_R16G16_FLOAT:        m_formatQualifier = "rg16f";          m_internalFormat = GL_RG16F;          m_format = GL_RG;           m_type = GL_HALF_FLOAT;                   m_components = 2; break;
		case DDS_DXGI_FORMAT_R32G32_FLOAT:        m_formatQualifier = "rg32f";          m_internalFormat = GL_RG32F;          m_format = GL_RG;           m_type = GL_FLOAT;                        m_components = 2; break;
		case DDS_DXGI_FORMAT_R8G8B8A8_UNORM:      m_formatQualifier = "rgba8";          m_internalFormat = GL_RGBA8;          m_format = GL_RGBA;         m_type = GL_UNSIGNED_BYTE;                m_components = 4; break;
		case DDS_DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: m_formatQualifier = "rgba8";          m_internalFormat = GL_SRGB8_ALPHA8;   m_format = GL_RGBA;         m_type = GL_UNSIGNED_BYTE;                m_components = 4; break;
		case DDS_DXGI_FORMAT_R8G8B8A8_SNORM:      m_formatQualifier = "rgba8_snorm";    m_internalFormat = GL_RGBA8_SNORM;    m_format = GL_RGBA;         m_type = GL_BYTE;                         m_components = 4; break;
		case DDS_DXGI_FORMAT_B8G8R8A8_UNORM:      m_formatQualifier = "rgba8";          m_internalFormat = GL_RGBA8;          m_format = GL_BGRA;         m_type = GL_UNSIGNED_BYTE;                m_components = 4; break; // compatible with Pixel32
		case DDS_DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: m_formatQualifier = "rgba8";          m_internalFormat = GL_SRGB8_ALPHA8;   m_format = GL_BGRA;         m_type = GL_UNSIGNED_BYTE;                m_components = 4; break;
		case DDS_DXGI_FORMAT_R16G16B16A16_UNORM:  m_formatQualifier = "rgba16";         m_internalFormat = GL_RGBA16;         m_format = GL_RGBA;         m_type = GL_UNSIGNED_SHORT;               m_components = 4; break;
		case DDS_DXGI_FORMAT_R16G16B16A16_SNORM:  m_formatQualifier = "rgba16_snorm";   m_internalFormat = GL_RGBA16_SNORM;   m_format = GL_RGBA;         m_type = GL_SHORT;                        m_components = 4; break;
		case DDS_DXGI_FORMAT_R16G16B16A16_FLOAT:  m_formatQualifier = "rgba16f";        m_internalFormat = GL_RGBA16F;        m_format = GL_RGBA;         m_type = GL_HALF_FLOAT;                   m_components = 4; break;
		case DDS_DXGI_FORMAT_R32G32B32A32_FLOAT:  m_formatQualifier = "rgba32f";        m_internalFormat = GL_RGBA32F;        m_format = GL_RGBA;         m_type = GL_FLOAT;                        m_components = 4; break;
		case DDS_DXGI_FORMAT_R32G32B32_FLOAT:     m_formatQualifier = "rgb32f";         m_internalFormat = GL_RGB32F;         m_format = GL_RGB;          m_type = GL_FLOAT;                        m_components = 3; break;
		case DDS_DXGI_FORMAT_R10G10B10A2_UNORM:   m_formatQualifier = "rgb10_a2";       m_internalFormat = GL_RGB10_A2;       m_format = GL_RGBA;         m_type = GL_UNSIGNED_INT_2_10_10_10_REV;  m_components = 4; break;
		case DDS_DXGI_FORMAT_R11G11B10_FLOAT:     m_formatQualifier = "r11f_g11f_b10f"; m_internalFormat = GL_R11F_G11F_B10F; m_format = GL_RGB;          m_type = GL_UNSIGNED_INT_10F_11F_11F_REV; m_components = 3; break;
		case DDS_DXGI_FORMAT_R9G9B9E5_SHAREDEXP:  m_formatQualifier = "unknown";        m_internalFormat = GL_RGB9_E5;        m_format = GL_RGB;          m_type = GL_UNSIGNED_INT_5_9_9_9_REV;     m_components = 3; break;
		case DDS_DXGI_FORMAT_B5G6R5_UNORM:        m_formatQualifier = "unknown";        m_internalFormat = GL_RGB565;         m_format = GL_RGB;          m_type = GL_UNSIGNED_SHORT_5_6_5;         m_components = 3; break;
		case DDS_DXGI_FORMAT_B5G5R5A1_UNORM:      m_formatQualifier = "unknown";        m_internalFormat = GL_RGB5_A1;        m_format = GL_BGRA;         m_type = GL_UNSIGNED_SHORT_1_5_5_5_REV;   m_components = 4; break;
		case DDS_DXGI_FORMAT_B4G4R4A4_UNORM:      m_formatQualifier = "unknown";        m_internalFormat = GL_RGBA4;          m_format = GL_BGRA;         m_type = GL_UNSIGNED_SHORT_4_4_4_4_REV;   m_components = 4; break;
		// unsigned integer
		case DDS_DXGI_FORMAT_R8_UINT:             m_formatQualifier = "r8ui";           m_internalFormat = GL_R8UI;           m_format = GL_RED_INTEGER;  m_type = GL_UNSIGNED_BYTE;                m_components = 1; m_samplerType = SAMPLER_TYPE_UNSIGNED_INT; break;
		case DDS_DXGI_FORMAT_R8G8_UINT:           m_formatQualifier = "rg8ui";          m_internalFormat = GL_RG8UI;          m_format = GL_RG_INTEGER;   m_type = GL_UNSIGNED_BYTE;                m_components = 2; m_samplerType = SAMPLER_TYPE_UNSIGNED_INT; break;
		case DDS_DXGI_FORMAT_R8G8B8A8_UINT:       m_formatQualifier = "rgba8ui";        m_internalFormat = GL_RGBA8UI;        m_format = GL_RGBA_INTEGER; m_type = GL_UNSIGNED_BYTE;                m_components = 4; m_samplerType = SAMPLER_TYPE_UNSIGNED_INT; break;
		case DDS_DXGI_FORMAT_R16_UINT:            m_formatQualifier = "r16ui";          m_internalFormat = GL_R16UI;          m_format = GL_RED_INTEGER;  m_type = GL_UNSIGNED_SHORT;               m_components = 1; m_samplerType = SAMPLER_TYPE_UNSIGNED_INT; break;
		case DDS_DXGI_FORMAT_R16G16_UINT:         m_formatQualifier = "rg16ui";         m_internalFormat = GL_RG16UI;         m_format = GL_RG_INTEGER;   m_type = GL_UNSIGNED_SHORT;               m_components = 2; m_samplerType = SAMPLER_TYPE_UNSIGNED_INT; break;
		case DDS_DXGI_FORMAT_R16G16B16A16_UINT:   m_formatQualifier = "rgba16ui";       m_internalFormat = GL_RGBA16UI;       m_format = GL_RGBA_INTEGER; m_type = GL_UNSIGNED_SHORT;               m_components = 4; m_samplerType = SAMPLER_TYPE_UNSIGNED_INT; break;
		case DDS_DXGI_FORMAT_R32_UINT:            m_formatQualifier = "r32ui";          m_internalFormat = GL_R32UI;          m_format = GL_RED_INTEGER;  m_type = GL_UNSIGNED_INT;                 m_components = 1; m_samplerType = SAMPLER_TYPE_UNSIGNED_INT; break;
		case DDS_DXGI_FORMAT_R32G32_UINT:         m_formatQualifier = "rg32ui";         m_internalFormat = GL_RG32UI;         m_format = GL_RG_INTEGER;   m_type = GL_UNSIGNED_INT;                 m_components = 2; m_samplerType = SAMPLER_TYPE_UNSIGNED_INT; break;
		case DDS_DXGI_FORMAT_R32G32B32A32_UINT:   m_formatQualifier = "rgba32ui";       m_internalFormat = GL_RGBA32UI;       m_format = GL_RGBA_INTEGER; m_type = GL_UNSIGNED_INT;                 m_components = 4; m_samplerType = SAMPLER_TYPE_UNSIGNED_INT; break;
		case DDS_DXGI_FORMAT_R10G10B10A2_UINT:    m_formatQualifier = "rgb10_a2ui";     m_internalFormat = GL_RGB10_A2UI;     m_format = GL_RGBA_INTEGER; m_type = GL_UNSIGNED_INT_2_10_10_10_REV;  m_components = 4; m_samplerType = SAMPLER_TYPE_UNSIGNED_INT; break; // untested!
		// signed integer
		case DDS_DXGI_FORMAT_R8_SINT:             m_formatQualifier = "r8i";            m_internalFormat = GL_R8I;            m_format = GL_RED_INTEGER;  m_type = GL_BYTE;                         m_components = 1; m_samplerType = SAMPLER_TYPE_SIGNED_INT; break;
		case DDS_DXGI_FORMAT_R8G8_SINT:           m_formatQualifier = "rg8i";           m_internalFormat = GL_RG8I;           m_format = GL_RG_INTEGER;   m_type = GL_BYTE;                         m_components = 2; m_samplerType = SAMPLER_TYPE_SIGNED_INT; break;
		case DDS_DXGI_FORMAT_R8G8B8A8_SINT:       m_formatQualifier = "rgba8i";         m_internalFormat = GL_RGBA8I;         m_format = GL_RGBA_INTEGER; m_type = GL_BYTE;                         m_components = 4; m_samplerType = SAMPLER_TYPE_SIGNED_INT; break;
		case DDS_DXGI_FORMAT_R16_SINT:            m_formatQualifier = "r16i";           m_internalFormat = GL_R16I;           m_format = GL_RED_INTEGER;  m_type = GL_SHORT;                        m_components = 1; m_samplerType = SAMPLER_TYPE_SIGNED_INT; break;
		case DDS_DXGI_FORMAT_R16G16_SINT:         m_formatQualifier = "rg16i";          m_internalFormat = GL_RG16I;          m_format = GL_RG_INTEGER;   m_type = GL_SHORT;                        m_components = 2; m_samplerType = SAMPLER_TYPE_SIGNED_INT; break;
		case DDS_DXGI_FORMAT_R16G16B16A16_SINT:   m_formatQualifier = "rgba16i";        m_internalFormat = GL_RGBA16I;        m_format = GL_RGBA_INTEGER; m_type = GL_SHORT;                        m_components = 4; m_samplerType = SAMPLER_TYPE_SIGNED_INT; break;
		case DDS_DXGI_FORMAT_R32_SINT:            m_formatQualifier = "r32i";           m_internalFormat = GL_R32I;           m_format = GL_RED_INTEGER;  m_type = GL_INT;                          m_components = 1; m_samplerType = SAMPLER_TYPE_SIGNED_INT; break;
		case DDS_DXGI_FORMAT_R32G32_SINT:         m_formatQualifier = "rg32i";          m_internalFormat = GL_RG32I;          m_format = GL_RG_INTEGER;   m_type = GL_INT;                          m_components = 2; m_samplerType = SAMPLER_TYPE_SIGNED_INT; break;
		case DDS_DXGI_FORMAT_R32G32B32A32_SINT:   m_formatQualifier = "rgba32i";        m_internalFormat = GL_RGBA32I;        m_format = GL_RGBA_INTEGER; m_type = GL_INT;                          m_components = 4; m_samplerType = SAMPLER_TYPE_SIGNED_INT; break;
		// block compressed
		case DDS_DXGI_FORMAT_BC1_UNORM:           m_internalFormat = GL_COMPRESSED_RGB_S3TC_DXT1_EXT;        m_components = 4; m_compressed = true; break;
		case DDS_DXGI_FORMAT_BC1_UNORM_SRGB:      m_internalFormat = GL_COMPRESSED_SRGB_S3TC_DXT1_EXT;       m_components = 4; m_compressed = true; break;
		case DDS_DXGI_FORMAT_BC2_UNORM:           m_internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;       m_components = 4; m_compressed = true; break;
		case DDS_DXGI_FORMAT_BC2_UNORM_SRGB:      m_internalFormat = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT; m_components = 4; m_compressed = true; break;
		case DDS_DXGI_FORMAT_BC3_UNORM:           m_internalFormat = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;       m_components = 4; m_compressed = true; break;
		case DDS_DXGI_FORMAT_BC3_UNORM_SRGB:      m_internalFormat = GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT; m_components = 4; m_compressed = true; break;
		case DDS_DXGI_FORMAT_BC4_UNORM:           m_internalFormat = GL_COMPRESSED_RED_RGTC1;                m_components = 1; m_compressed = true; break;
		case DDS_DXGI_FORMAT_BC4_SNORM:           m_internalFormat = GL_COMPRESSED_SIGNED_RED_RGTC1;         m_components = 1; m_compressed = true; break;
		case DDS_DXGI_FORMAT_BC5_UNORM:           m_internalFormat = GL_COMPRESSED_RG_RGTC2;                 m_components = 2; m_compressed = true; break;
		case DDS_DXGI_FORMAT_BC5_SNORM:           m_internalFormat = GL_COMPRESSED_SIGNED_RG_RGTC2;          m_components = 2; m_compressed = true; break;
		case DDS_DXGI_FORMAT_BC6H_UF16:           m_internalFormat = GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT;  m_components = 3; m_compressed = true; break;
		case DDS_DXGI_FORMAT_BC6H_SF16:           m_internalFormat = GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT;    m_components = 3; m_compressed = true; break;
		case DDS_DXGI_FORMAT_BC7_UNORM:           m_internalFormat = GL_COMPRESSED_RGBA_BPTC_UNORM;          m_components = 4; m_compressed = true; break;
		case DDS_DXGI_FORMAT_BC7_UNORM_SRGB:      m_internalFormat = GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM;    m_components = 4; m_compressed = true; break;

		// =============================================================
		// notable DXGI formats missing:
		// all "typeless" formats (they don't really belong here)
		// all formats with "X" (e.g. R8G8B8X8)
		// all formats with "D" (e.g. D32_FLOAT)
		// interleaved formats (R8G8_B8G8 and G8R8_G8B8)
		// R10G10B10_XR_BIAS_A2_UNORM
		// 7E3 and 6E4 compressed float formats
		// R10G10B10_SNORM_A2_UNORM and R4G4_UNORM, and R1_UNORM formats
		// =============================================================
		default: ForceAssert(false);
		}

		const bool isTypeUInt = (m_type == GL_UNSIGNED_BYTE || m_type == GL_UNSIGNED_SHORT || m_type == GL_UNSIGNED_INT || m_type == GL_UNSIGNED_INT_2_10_10_10_REV);
		const bool isTypeSInt = (m_type == GL_BYTE || m_type == GL_SHORT || m_type == GL_INT);
		switch (m_format) {
		case GL_RED:  m_formatType = "float"; break;
		case GL_RG:   m_formatType = "vec2";  break;
		case GL_RGB:  m_formatType = "vec3";  break;
		case GL_RGBA: m_formatType = "vec4";  break;
		case GL_BGRA: m_formatType = "vec4";  break;
		case GL_RED_INTEGER:  m_formatType = isTypeUInt ? "uint"  : "int";   break;
		case GL_RG_INTEGER:   m_formatType = isTypeUInt ? "uvec2" : "ivec2"; break;
		case GL_RGB_INTEGER:  m_formatType = isTypeUInt ? "uvec3" : "ivec3"; break;
		case GL_RGBA_INTEGER: m_formatType = isTypeUInt ? "uvec4" : "ivec4"; break;
		}
	}

	const char* m_formatQualifier; // e.g. "rgba8"
	const char* m_formatType; // e.g. "vec4"
	GLenum m_internalFormat; // e.g. GL_RGBA8
	GLenum m_format; // e.g. GL_RGBA
	GLenum m_type; // e.g. GL_UNSIGNED_BYTE
	eSamplerType m_samplerType;
	uint32 m_components;
	bool m_compressed;
};

static std::string GetOpenGLSamplerTypeStr(GLenum target, DDS_DXGI_FORMAT format, bool isShadowSampler = false, bool isImageSampler = false, bool isLayered = true)
{
	if (!isLayered) {
		switch (target) {
		case GL_TEXTURE_1D_ARRAY:             target = GL_TEXTURE_1D; break;
		case GL_TEXTURE_2D_ARRAY:             target = GL_TEXTURE_2D; break; // once layer of array texture
		case GL_TEXTURE_2D_MULTISAMPLE_ARRAY: target = GL_TEXTURE_2D_MULTISAMPLE; break;
		case GL_TEXTURE_3D:                   target = GL_TEXTURE_2D; break; // one slice of 3D texture
		case GL_TEXTURE_CUBE_MAP:             target = GL_TEXTURE_2D; break; // once face of cube map texture
		case GL_TEXTURE_CUBE_MAP_ARRAY:       target = GL_TEXTURE_2D; break;
		}
	}
	const char* samplerTypeStr = "samplerUnknown";
	if (isShadowSampler) {
		switch (target) {
		case GL_TEXTURE_1D:                   samplerTypeStr = "sampler1DShadow"; break;
		case GL_TEXTURE_1D_ARRAY:             samplerTypeStr = "sampler1DArrayShadow"; break;
		case GL_TEXTURE_2D:                   samplerTypeStr = "sampler2DShadow"; break;
		case GL_TEXTURE_2D_ARRAY:             samplerTypeStr = "sampler2DArrayShadow"; break;
		case GL_TEXTURE_CUBE_MAP:             samplerTypeStr = "samplerCubeShadowShadow"; break;
		case GL_TEXTURE_CUBE_MAP_ARRAY:       samplerTypeStr = "samplerCubeArrayShadow"; break;
		case GL_TEXTURE_RECTANGLE:            samplerTypeStr = "sampler2DRectShadow"; break;
		}
	} else {
		switch (target) {
		case GL_TEXTURE_1D:                   samplerTypeStr = "sampler1D"; break;
		case GL_TEXTURE_1D_ARRAY:             samplerTypeStr = "sampler1DArray"; break;
		case GL_TEXTURE_2D:                   samplerTypeStr = "sampler2D"; break;
		case GL_TEXTURE_2D_ARRAY:             samplerTypeStr = "sampler2DArray"; break;
		case GL_TEXTURE_2D_MULTISAMPLE:       samplerTypeStr = "sampler2DMS"; break;
		case GL_TEXTURE_2D_MULTISAMPLE_ARRAY: samplerTypeStr = "sampler2DMSArray"; break;
		case GL_TEXTURE_3D:                   samplerTypeStr = "sampler3D"; break;
		case GL_TEXTURE_CUBE_MAP:             samplerTypeStr = "samplerCube"; break;
		case GL_TEXTURE_CUBE_MAP_ARRAY:       samplerTypeStr = "samplerCubeArray"; break;
		case GL_TEXTURE_RECTANGLE:            samplerTypeStr = "sampler2DRect"; break;
		case GL_TEXTURE_BUFFER:               samplerTypeStr = "samplerBuffer"; break;
		}
	}
	if (isImageSampler) {
		static char temp[64];
		sprintf(temp, "image%s", samplerTypeStr + strlen("sampler")); // replace 'sampler' with 'image'
		samplerTypeStr = temp;
	}
	const char* samplerTypePrefix = "";
	const TextureFormatInfo info(format);
	if (info.m_samplerType == TextureFormatInfo::SAMPLER_TYPE_UNSIGNED_INT)
		samplerTypePrefix = "u";
	else if (info.m_samplerType == TextureFormatInfo::SAMPLER_TYPE_SIGNED_INT)
		samplerTypePrefix = "i";
	return varString("%s%s", samplerTypePrefix, samplerTypeStr);
}

static void BindTextureTarget(GLuint programID, GLuint textureID, GLenum target, uint32 slot, const char* varName, uint32 varIndex = 0)
{
	ForceAssert(slot < 32);
	glActiveTexture(GL_TEXTURE0 + slot);
	glBindTexture(target, textureID);
	const GLint location = glGetUniformLocation(programID, varName);
	if (location != -1)
		glUniform1i(location + varIndex, slot);
	glActiveTexture(GL_TEXTURE0); // restore
}

static uint32 g_Frame = 0;
static float g_Time = 0.0f;
static float g_TimeDelta = 0.0f;

static void UpdateFrameTime()
{
	static uint64 time0 = 0;
	static uint64 time = 0;
	if (time == 0)
		time = time0 = ProgressDisplay::GetCurrentPerformanceTime();
	else {
		g_Time = ProgressDisplay::GetTimeInSeconds(time0);
		g_TimeDelta = ProgressDisplay::GetDeltaTimeInSeconds(time);
	}
}

static bool LoadFileIntoStrings(std::vector<std::string>& strings, const char* path)
{
	FILE* file = fopen(path, "r");
	if (file) {
		char line[8192];
		while (fgets(line, sizeof(line), file)) {
			char* end = strstr(line, "\r\n");
			if (end)
				*end = '\0';
			else {
				end = strrchr(line, '\n');
				if (end && end[1] == '\0')
					*end = '\0';
			}
			strings.push_back(line);
		}
		fclose(file);
		return true;
	} else
		return false;
}

template <typename PixelType> static void Downsample2D(PixelType* dstImage, uint32 dstW, uint32 dstH, const PixelType* srcImage, uint32 srcW, uint32 srcH)
{
	if (dstW == srcW && dstH == srcH)
		memcpy(dstImage, srcImage, srcW*srcH*sizeof(PixelType));
	else {
		ForceAssert(dstW <= srcW && dstH <= srcH);
		const uint32 sx = srcW/dstW;
		const uint32 sy = srcH/dstH;
		for (uint32 mj = 0; mj < dstH; mj++) {
			for (uint32 mi = 0; mi < dstW; mi++) {
				Vec4V sum(V_ZERO);
				uint32 count = 0;
				for (uint32 jj = 0; jj < sy; jj++) {
					for (uint32 ii = 0; ii < sx; ii++) {
						const uint32 i = ii + mi*sx;
						const uint32 j = jj + mj*sy;
						if (i < srcW && j < srcH) {
							sum += srcImage[i + j*srcW];
							count++;
						}
					}
				}
				if (count > 0)
					sum *= 1.0f/(float)count;
				dstImage[mi + mj*dstW] = sum;
			}
		}
	}
}

static void ReshapeFunc(int width, int height)
{
	g_ViewportWidth = width;
	g_ViewportHeight = height;
	g_Frame = 0; // iFrame does not get reset when window changes in ShaderToy .. but i find this behavior useful
	glutReshapeWindow(width, height);
}

static const char* GetName(const NameValuePairs* nvp)
{
	if (nvp && nvp->size() > 0 && (nvp->operator[](0).m_name == std::string("name") || nvp->operator[](0).m_name.empty()))
		return nvp->operator[](0).m_value.c_str();
	else
		return nullptr;
}

// e.g. //$BUFFER: name=variance, relative_width=0.25, relative_height=0.25, format=R32G32B32_FLOAT, filter=OFF
class ShaderToyBuffer
{
public:
	enum
	{
		KEYBOARD_INPUT_TEXTURE_WIDTH = 256,
		KEYBOARD_INPUT_TEXTURE_HEIGHT = 3, // y=0..2 (0=down,1=pressed,2=toggled)

		KEYBOARD2_INPUT_TEXTURE_WIDTH = 256,
		KEYBOARD2_INPUT_TEXTURE_HEIGHT = 10,
	};

	class Desc
	{
	public:
		Desc()
			: m_hash(0)
			, m_name("")
			, m_path("")
			, m_resolutionX(0)
			, m_resolutionY(0)
			, m_resolutionZ(0)
			, m_relativeResX(0.0f)
			, m_relativeResY(0.0f)
			, m_numLayers(0)
			, m_mipLevels(0)
			, m_isCubemap(false)
			, m_format(DDS_DXGI_FORMAT_UNKNOWN)
			, m_filter(false)
			, m_wrap(false)
		{}

		bool IsImmutable() const
		{
			return Max(m_relativeResX, m_relativeResY) <= 0.0f;
		}

		void CalculateHash()
		{
			ForceAssert(m_hash == 0);
			uint64 hash = 0;
			hash = Crc64(m_name.c_str(), m_name.size(), hash);
			hash = Crc64(m_path.c_str(), m_path.size(), hash);
			hash = Crc64(m_resolutionX, hash);
			hash = Crc64(m_resolutionY, hash);
			hash = Crc64(m_resolutionZ, hash);
			hash = Crc64(m_relativeResX, hash);
			hash = Crc64(m_relativeResY, hash);
			hash = Crc64(m_numLayers, hash);
			hash = Crc64(m_mipLevels, hash);
			hash = Crc64(m_isCubemap, hash);
			hash = Crc64(m_format, hash);
			hash = Crc64(m_filter, hash);
			hash = Crc64(m_wrap, hash);
			m_hash = hash;
		}

		void Print(const char* indent = "") const
		{
			printf("%sm_hash = 0x%08x%08x\n", indent, (uint32)(m_hash >> 32), (uint32)m_hash);
			printf("%sm_name = \"%s\"\n", indent, m_name.c_str());
			printf("%sm_path = \"%s\"\n", indent, m_path.c_str());
			printf("%sm_resolutionX = %u\n", indent, m_resolutionX);
			printf("%sm_resolutionY = %u\n", indent, m_resolutionY);
			printf("%sm_resolutionZ = %u\n", indent, m_resolutionZ);
			printf("%sm_relativeResX = %f\n", indent, m_relativeResX);
			printf("%sm_relativeResY = %f\n", indent, m_relativeResY);
			printf("%sm_numLayers = %u\n", indent, m_numLayers);
			printf("%sm_mipLevels = %u\n", indent, m_mipLevels);
			printf("%sm_isCubemap = %s\n", indent, m_isCubemap ? "TRUE" : "FALSE");
			printf("%sm_format = %s\n", indent, GetDX10FormatStr(m_format, true));
			printf("%sm_filter = %s\n", indent, m_filter ? "TRUE" : "FALSE");
			printf("%sm_wrap = %s\n", indent, m_wrap ? "TRUE" : "FALSE");
		}

		uint64 m_hash;
		std::string m_name;
		std::string m_path;
		uint32 m_resolutionX; // specified resolution
		uint32 m_resolutionY;
		uint32 m_resolutionZ;
		float m_relativeResX; // if >0, xy resolution will be relative to main viewport
		float m_relativeResY;
		uint32 m_numLayers;
		uint32 m_mipLevels;
		bool m_isCubemap; // TODO
		DDS_DXGI_FORMAT m_format;
		bool m_filter;
		bool m_wrap;
	};

	static ShaderToyBuffer* Add(int passIndex, const char* name, const NameValuePairs* nvp = nullptr)
	{
		const char* bufferDescParams[] = {
			"path",
			"width",
			"height",
			"depth",
			"relative_width",
			"relative_height",
			"layers",
			"mips",
			"format",
			"filter",
			"wrap",
		};
		Desc desc;
		desc.m_name = name;
		if (strcmp(name, "[KEYBOARD]") == 0) {
			desc.m_path = "";
			desc.m_resolutionX = KEYBOARD_INPUT_TEXTURE_WIDTH;
			desc.m_resolutionY = KEYBOARD_INPUT_TEXTURE_HEIGHT;
			desc.m_resolutionZ = 1;
			desc.m_relativeResX = 0.0f;
			desc.m_relativeResY = 0.0f;
			desc.m_numLayers = 1;
			desc.m_mipLevels = 1;
			desc.m_isCubemap = false;
			desc.m_format = DDS_DXGI_FORMAT_R8_UNORM;
			desc.m_filter = false;
			desc.m_wrap = false;
		} else if (strcmp(name, "[KEYBOARD2]") == 0) {
			desc.m_path = "";
			desc.m_resolutionX = KEYBOARD2_INPUT_TEXTURE_WIDTH;
			desc.m_resolutionY = KEYBOARD2_INPUT_TEXTURE_HEIGHT;
			desc.m_resolutionZ = 1;
			desc.m_relativeResX = 0.0f;
			desc.m_relativeResY = 0.0f;
			desc.m_numLayers = 1;
			desc.m_mipLevels = 1;
			desc.m_isCubemap = false;
			desc.m_format = DDS_DXGI_FORMAT_R32_UINT;
			desc.m_filter = false;
			desc.m_wrap = false;
		} else {
			desc.m_path = nvp->GetStringValue("path", "");
			desc.m_resolutionX = nvp->GetUIntValue("width");
			desc.m_resolutionY = nvp->GetUIntValue("height");
			desc.m_resolutionZ = nvp->GetUIntValue("depth", 1);
			desc.m_relativeResX = nvp->GetFloatValue("relative_width");
			desc.m_relativeResY = nvp->GetFloatValue("relative_height");
			desc.m_numLayers = nvp->GetUIntValue("layers", 1);
			desc.m_mipLevels = nvp->GetUIntValue("mips", 1);
			desc.m_isCubemap = false;
			desc.m_format = GetDX10FormatFromString(nvp->GetStringValue("format", "UNKNOWN"));
			desc.m_filter = nvp->GetBoolValue("filter", !desc.m_path.empty());
			desc.m_wrap = nvp->GetBoolValue("wrap");
		}

		// defaults
		if (desc.m_relativeResX <= 0.0f && desc.m_resolutionX == 0 && desc.m_path.empty())
			desc.m_relativeResX = 1.0f;
		if (desc.m_relativeResY <= 0.0f && desc.m_resolutionY == 0 && desc.m_path.empty())
			desc.m_relativeResY = 1.0f;
		if (desc.m_mipLevels == 0)
			desc.m_mipLevels = 1;
		if (desc.m_format == DDS_DXGI_FORMAT_UNKNOWN)
			desc.m_format = desc.m_path.empty() ? DDS_DXGI_FORMAT_R32G32B32A32_FLOAT : DDS_DXGI_FORMAT_R8G8B8A8_UNORM;
	#if 1 // my laptop (GeForce 940MX)
		uint32 maxTextureRes2D = 16384;
		uint32 maxTextureRes3D = 2048;
		uint32 maxTextureLayers = 2048;
	#else // my work desktop (RTX 2080)
		uint32 maxTextureRes2D = 32768;
		uint32 maxTextureRes3D = 16384;
		uint32 maxTextureLayers = 2048;
	#endif
		if (1) { // query opengl implementation
			glGetIntegerv(GL_MAX_TEXTURE_SIZE, (GLint*)&maxTextureRes2D);
			glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, (GLint*)&maxTextureRes3D);
			glGetIntegerv(GL_MAX_ARRAY_TEXTURE_LAYERS, (GLint*)&maxTextureLayers);
			static bool once = true;
			if (once) {
				once = false;
				printf("GL_MAX_TEXTURE_SIZE = %u\n", maxTextureRes2D);
				printf("GL_MAX_3D_TEXTURE_SIZE = %u\n", maxTextureRes3D);
				printf("GL_MAX_ARRAY_TEXTURE_LAYERS = %u\n", maxTextureLayers);
			}
		}
		if (desc.m_resolutionZ > 1) {
			if (Max(desc.m_resolutionX, desc.m_resolutionY, desc.m_resolutionZ) > maxTextureRes3D) {
				printf("warning: 3D texture \"%s\" resolution %ux%ux%u exceeded maximum %u, clamping ..\n", name, desc.m_resolutionX, desc.m_resolutionY, desc.m_resolutionZ, maxTextureRes3D);
				desc.m_resolutionX = Min(desc.m_resolutionX, maxTextureRes3D);
				desc.m_resolutionY = Min(desc.m_resolutionY, maxTextureRes3D);
				desc.m_resolutionZ = Min(desc.m_resolutionZ, maxTextureRes3D);
			}
			if (desc.m_numLayers > 1) {
				printf("warning: 3D texture \"%s\" specified with %u layers, cannot have layers ..\n", name, desc.m_numLayers);
				desc.m_numLayers = 1;
			}
		} else if (Max(desc.m_resolutionX, desc.m_resolutionY) > maxTextureRes2D) {
			printf("warning: 2D texture \"%s\" resolution %ux%u exceeded maximum %u, clamping ..\n", name, desc.m_resolutionX, desc.m_resolutionY, maxTextureRes2D);
			desc.m_resolutionX = Min(desc.m_resolutionX, maxTextureRes2D);
			desc.m_resolutionY = Min(desc.m_resolutionY, maxTextureRes2D);
		}
		if (desc.m_numLayers > maxTextureLayers) {
			printf("warning: texture \"%s\" specified with %u layers exceeded maximum %u, clamping ..\n", name, desc.m_numLayers, maxTextureLayers);
			desc.m_numLayers = maxTextureLayers;
		}
		if (desc.m_mipLevels > 1 && !desc.IsImmutable()) {
			printf("warning: non-immutable texture \"%s\" specified with %u mips, cannot have mips .. (wait, why?)\n", name, desc.m_mipLevels);
			desc.m_mipLevels = 1;
		}
		if (desc.m_path.empty()) {
			ForceAssert((desc.m_relativeResX == 0.0f && desc.m_resolutionX > 0) || (desc.m_relativeResX > 0.0f && desc.m_resolutionX == 0)); 
			ForceAssert((desc.m_relativeResY == 0.0f && desc.m_resolutionY > 0) || (desc.m_relativeResY > 0.0f && desc.m_resolutionY == 0)); 
		} else {
			ForceAssert(desc.m_relativeResX == 0.0f && desc.m_relativeResY == 0.0f);
			//ForceAssert(Max(desc.m_resolutionX, desc.m_resolutionY) == 0 && desc.m_resolutionZ == 1); // texture gets resolution from image file
			//ForceAssert(desc.m_numLayers == 1);
			// TODO -- might be nice to allow for arrays or volumes (or mips) stored as atlases ..
		}

		ShaderToyBuffer* buffer = Find(name);
		Vec4V* image = nullptr;
		if (!desc.m_path.empty()) {
			uint32 w = 0;
			uint32 h = 0;
			if (FileExists(desc.m_path.c_str()))
				image = LoadImage_Vec4V(desc.m_path.c_str(), (int&)w, (int&)h);
			if (image) {
				desc.m_resolutionX = w;
				desc.m_resolutionY = h;
				desc.m_mipLevels = Min(Log2FloorInt(Max(w, h)) + 1U, desc.m_mipLevels);
			} else {
				desc.m_path = "[DEFAULT]";
				desc.m_resolutionX = w = 4; // tiny black square
				desc.m_resolutionY = h = 4;
				image = new Vec4V[w*h];
				memset(image, 0, w*h*sizeof(Vec4V));
			}
		}
		desc.CalculateHash();
		if (buffer) {
			if (desc.m_hash != buffer->m_desc.m_hash && nvp && nvp->size() > 1) {
				// let's see if there are actually any buffer description params - nvp might just contain other params (e.g. layer=xxx for outputs)
				bool hasBufferDescParams = false;
				for (int i = 0; i < icountof(bufferDescParams); i++) {
					if (nvp->HasValue(bufferDescParams[i])) {
						hasBufferDescParams = true;
						break;
					}
				}
				if (hasBufferDescParams) {
					printf("warning: buffer hash mismatch for \"%s\" in pass %i!\n", name, passIndex);
					printf("existing buffer was:\n");
					buffer->m_desc.Print("\t");
					printf("new buffer is:\n");
					desc.Print("\t");
				}
			}
		} else {
			buffer = new ShaderToyBuffer();
			buffer->m_desc = desc;
			buffer->m_res[0] = 0;
			buffer->m_res[1] = 0;
			buffer->m_res[2] = 0;
			buffer->m_textureID = 0;
			buffer->m_target = GL_NONE;
			buffer->Update(image);
			GetMap()[name] = buffer;
		}
		if (image)
			delete[] image;
		return buffer;
	}

	void Update(const Vec4V* image = nullptr)
	{
		const uint32 w = m_desc.m_relativeResX <= 0.0f ? m_desc.m_resolutionX : (uint32)Ceiling(m_desc.m_relativeResX*(float)g_ViewportWidth);
		const uint32 h = m_desc.m_relativeResY <= 0.0f ? m_desc.m_resolutionY : (uint32)Ceiling(m_desc.m_relativeResY*(float)g_ViewportHeight);
		const uint32 d = m_desc.m_resolutionZ;
		if (m_res[0] != w || m_res[1] != h) { // resolution changed (because window size changed), or needs setup
			m_res[0] = w;
			m_res[1] = h;
			m_res[2] = d;
			m_target = m_desc.m_resolutionZ > 1 ? GL_TEXTURE_3D : (m_desc.m_numLayers > 1 ? GL_TEXTURE_2D_ARRAY : GL_TEXTURE_2D);
			m_mipLevels = Min(Log2FloorInt(Max(w, h, d)) + 1U, m_desc.m_mipLevels); // actual mip levels
			const bool isArrayOr3D = m_desc.m_resolutionZ > 1 || m_desc.m_numLayers > 1;
			const TextureFormatInfo info(m_desc.m_format);
			if (m_textureID == 0) {
				glGenTextures(1, &m_textureID);
				glBindTexture(m_target, m_textureID);
				glTexParameteri(m_target, GL_TEXTURE_MAG_FILTER, m_desc.m_filter ? GL_LINEAR : GL_NEAREST);
				glTexParameteri(m_target, GL_TEXTURE_MIN_FILTER, m_desc.m_filter ? (m_mipLevels > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR) : GL_NEAREST);
				glTexParameteri(m_target, GL_TEXTURE_MAX_LEVEL, m_mipLevels - 1);
				glTexParameteri(m_target, GL_TEXTURE_WRAP_S, m_desc.m_wrap ? GL_REPEAT : GL_CLAMP_TO_EDGE);
				glTexParameteri(m_target, GL_TEXTURE_WRAP_T, m_desc.m_wrap ? GL_REPEAT : GL_CLAMP_TO_EDGE);
				if (m_target == GL_TEXTURE_3D)
					glTexParameteri(m_target, GL_TEXTURE_WRAP_R, m_desc.m_wrap ? GL_REPEAT : GL_CLAMP_TO_EDGE);
			} else
				glBindTexture(m_target, m_textureID);
			if (m_desc.IsImmutable()) {
				const uint32 numLayersOrSlices = m_desc.m_resolutionZ > 1 ? d : m_desc.m_numLayers;
				if (isArrayOr3D)
					glTexStorage3D(m_target, m_mipLevels, info.m_internalFormat, w, h, numLayersOrSlices);
				else {
					glTexStorage2D(m_target, m_mipLevels, info.m_internalFormat, w, h);
					if (image) {
						const uint32 bs = GetDX10FormatBlockSize(m_desc.m_format);
						const uint32 blockSizeInBytes = (GetDX10FormatBitsPerPixel(m_desc.m_format)*bs*bs)/8;
						Vec4V* mipImage = nullptr;
						const Vec4V* src = image;
						void* temp = nullptr;
						for (uint32 mipIndex = 0; mipIndex < m_mipLevels; mipIndex++) {
							const uint32 mw = Max(1U, w >> mipIndex);
							const uint32 mh = Max(1U, h >> mipIndex);
							const uint32 bw = (mw + bs - 1)/bs;
							const uint32 bh = (mh + bs - 1)/bs;
							const uint32 imageSizeInBytes = bw*bh*blockSizeInBytes;
							if (mipIndex > 0) {
								if (mipImage == nullptr)
									mipImage = new Vec4V[mw*mh];
								Downsample2D(mipImage, mw, mh, image, w, h);
								src = mipImage;
							}
							if (temp == nullptr)
								temp = new char[imageSizeInBytes];
							const bool sRGB =
								m_desc.m_format == DDS_DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
								m_desc.m_format == DDS_DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ||
								m_desc.m_format == DDS_DXGI_FORMAT_B8G8R8X8_UNORM_SRGB ||
								m_desc.m_format == DDS_DXGI_FORMAT_BC1_UNORM_SRGB      ||
								m_desc.m_format == DDS_DXGI_FORMAT_BC2_UNORM_SRGB      ||
								m_desc.m_format == DDS_DXGI_FORMAT_BC3_UNORM_SRGB      ||
								m_desc.m_format == DDS_DXGI_FORMAT_BC7_UNORM_SRGB;
							if (ForceAssertVerify(ConvertPixelsToDX10Format(temp, m_desc.m_format, src, mw, mh, sRGB))) {
								if (info.m_compressed)
									glCompressedTexSubImage2D(GL_TEXTURE_2D, mipIndex, 0, 0, mw, mh, info.m_internalFormat, imageSizeInBytes, temp);
								else
									glTexSubImage2D(GL_TEXTURE_2D, mipIndex, 0, 0, mw, mh, info.m_format, info.m_type, temp);
							}
						}
						if (mipImage)
							delete[] mipImage;
						if (temp)
							delete[] temp;
					}
				}
			} else {
				const GLint border = 0;
				for (uint32 mipIndex = 0; mipIndex < m_mipLevels; mipIndex++) {
					const uint32 mw = Max(1U, w >> mipIndex);
					const uint32 mh = Max(1U, h >> mipIndex);
					const uint32 md = Max(1U, d >> mipIndex);
					const uint32 numLayersOrSlices = m_desc.m_resolutionZ > 1 ? md : m_desc.m_numLayers;
					if (isArrayOr3D)
						glTexImage3D(m_target, mipIndex, info.m_internalFormat, mw, mh, numLayersOrSlices, border, info.m_format, info.m_type, nullptr);
					else
						glTexImage2D(m_target, mipIndex, info.m_internalFormat, mw, mh, border, info.m_format, info.m_type, nullptr);
				}
			}
			glBindTexture(m_target, 0); // restore
		}

		// update keyboard texture
		if (m_desc.m_name == std::string("[KEYBOARD]")) {
			const uint32 w = KEYBOARD_INPUT_TEXTURE_WIDTH;
			const uint32 h = KEYBOARD_INPUT_TEXTURE_HEIGHT;
			static uint8 keyToggled[(w + 7)>>3];
			static bool keyOnce = true;
			if (keyOnce) {
				keyOnce = false;
				memset(keyToggled, 0, sizeof(keyToggled));
			}
			for (uint32 i = 0; i < w; i++) {
				if (g_Keyboard.IsKeyPressed(i, Keyboard::MODIFIER_ANY))
					keyToggled[i>>3] ^= BIT(i&7);
			}
			static uint8* keyboardInputData = nullptr;
			if (keyboardInputData == nullptr)
				keyboardInputData = new uint8[w*h];
			memset(keyboardInputData, 0, w*h*sizeof(uint8));
			for (uint32 i = 0; i < w; i++) {
				if (g_Keyboard.IsKeyDown(i, Keyboard::MODIFIER_ANY))
					keyboardInputData[i + 0*w] = 255;
				if (g_Keyboard.IsKeyPressed(i, Keyboard::MODIFIER_ANY))
					keyboardInputData[i + 1*w] = 255;
				if (keyToggled[i>>3] & BIT(i&7))
					keyboardInputData[i + 2*w] = 255;
			}
			const TextureFormatInfo info(DDS_DXGI_FORMAT_R8_UNORM);
			glBindTexture(GL_TEXTURE_2D, m_textureID);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, info.m_format, info.m_type, keyboardInputData);
			glBindTexture(GL_TEXTURE_2D, 0);
		} else if (m_desc.m_name == std::string("[KEYBOARD2]")) {
			const uint32 w = KEYBOARD2_INPUT_TEXTURE_WIDTH;
			const uint32 h = KEYBOARD2_INPUT_TEXTURE_HEIGHT;
			static uint32* keyboardInputData = nullptr;
			if (keyboardInputData == nullptr) {
				keyboardInputData = new uint32[w*h];
				memset(keyboardInputData, 0, w*h*sizeof(uint32));
			}
			for (uint32 i = 0; i < w; i++) {
				for (uint32 modifiers = 0; modifiers < 8; modifiers++) {
					StaticAssert(BIT(0) == Keyboard::MODIFIER_SHIFT); // make sure Keyboard::MODIFIERS matches the layout here
					StaticAssert(BIT(1) == Keyboard::MODIFIER_CONTROL);
					StaticAssert(BIT(2) == Keyboard::MODIFIER_ALT);
					if (g_Keyboard.IsKeyPressed(i, modifiers))
						keyboardInputData[i + (KEYBOARD2_ROW_COUNTER_NO_MODIFIERS + modifiers)*w]++;
				}
				uint32 state = KEYBOARD2_STATE_UP;
				if (g_Keyboard.IsKeyPressed(i, Keyboard::MODIFIER_ANY)) {
					keyboardInputData[i + KEYBOARD2_ROW_COUNTER_ANY_MODIFIERS*w]++;
					state = KEYBOARD2_STATE_PRESSED;
				} else if (g_Keyboard.IsKeyReleased(i))
					state = KEYBOARD2_STATE_RELEASED;
				else if (g_Keyboard.IsKeyDown(i, Keyboard::MODIFIER_ANY))
					state = KEYBOARD2_STATE_DOWN;
				keyboardInputData[i + KEYBOARD2_ROW_STATE*w] = state;
			}
			const TextureFormatInfo info(DDS_DXGI_FORMAT_R32_UINT);
			glBindTexture(GL_TEXTURE_2D, m_textureID);
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, info.m_format, info.m_type, keyboardInputData);
			glBindTexture(GL_TEXTURE_2D, 0);
		}
	}

	static void UpdateAll()
	{
		std::map<std::string,ShaderToyBuffer*>& m = GetMap();
		for (auto it = m.begin(); it != m.end(); ++it)
			it->second->Update();
	}

	static std::map<std::string,ShaderToyBuffer*>& GetMap()
	{
		static std::map<std::string,ShaderToyBuffer*> m;
		return m;
	}

	static ShaderToyBuffer* Find(const char* name)
	{
		std::map<std::string,ShaderToyBuffer*>& m = GetMap();
		const auto f = m.find(name);
		if (f != m.end())
			return f->second;
		else
			return nullptr;
	}

	Desc m_desc;
	uint32 m_res[3];
	uint32 m_mipLevels; // actual mip levels
	GLuint m_textureID;
	GLenum m_target;
};

class ShaderToyRenderPass
{
public:
	enum
	{
		MAX_INPUTS = SHADERTOY_MAX_INPUT_CHANNELS,
		MAX_IMAGES = 8,
		MAX_PASSES = 256,
	};

	class PassInput
	{
	public:
		PassInput() : m_buffer(nullptr) {}
		ShaderToyBuffer* m_buffer;
	};

	class PassOutput
	{
	public:
		PassOutput() : m_buffer(nullptr), m_layerOrSliceIndex(0), m_mipIndex(0) {}
		ShaderToyBuffer* m_buffer;
		uint32 m_layerOrSliceIndex;
		uint32 m_mipIndex;
	};

#if SUPPORT_IMAGES
	class PassImage
	{
	public:
		PassImage() : m_buffer(nullptr), m_layered(false), m_layerOrSliceIndex(0), m_mipIndex(0), m_access(GL_NONE), m_internalFormat(GL_NONE) {}
		std::string m_name;
		ShaderToyBuffer* m_buffer;
		bool m_layered; // if true, ignore m_layerOrSliceIndex and bind the entire mip level as an array or 3D
		uint32 m_layerOrSliceIndex;
		uint32 m_mipIndex;
		GLenum m_access;
		GLenum m_internalFormat;
	};
#endif // SUPPORT_IMAGES

	ShaderToyRenderPass(uint32 passIndex, GLuint programID)
		: m_passIndex(passIndex)
		, m_programID(programID)
		, m_outputFramebufferID(0)
	{}

	void AddBuffer(char* s)
	{
		SkipLeadingWhitespace(s);
		if (*s++ == ':') {
			char* trailingComment = strstr(s, "//");
			if (trailingComment)
				*trailingComment = '\0';
			const NameValuePairs nvp(s);
			const char* name = GetName(&nvp);
			if (name)
				ShaderToyBuffer::Add(m_passIndex, name, &nvp);
			else
				printf("error: pass %u buffer description expected to start with name!\n", m_passIndex);
		} else
			printf("error: pass %u buffer not processed, missing ':'!\n", m_passIndex);
	}

	void AddInput(char* s)
	{
		// for ShaderToy support, we need to allow for specified input indices (maybe .. i dunno .. don't want to think about it too much)
		// for sanity's sake, please declare inputs with specified indices FIRST, before any inputs with unspecified indices
		const bool allowSpecificInputIndex = true;
		uint32 inputIndex = 0;
		if (allowSpecificInputIndex) {
			if (isdigit(*s)) {
				inputIndex = (uint32)atoi(s);
				while (isdigit(*s))
					s++;
				if (inputIndex >= m_inputs.size())
					m_inputs.resize(inputIndex + 1);
				else if (m_inputs[inputIndex].m_buffer) {
					printf("error: pass %u input specified index %u, but this input is already bound to a buffer!\n", m_passIndex, inputIndex);
					return;
				}
			} else { // unspecified input index - find the first one available
				inputIndex = (uint32)m_inputs.size();
				for (uint32 i = 0; i < m_inputs.size(); i++) {
					if (m_inputs[i].m_buffer == nullptr) {
						inputIndex = i;
						break;
					}
				}
				if (inputIndex == m_inputs.size())
					m_inputs.resize(inputIndex + 1);
			}
		} else {
			while (isdigit(*s))
				s++;
		}
		SkipLeadingWhitespace(s);
		if (*s++ == ':') {
			char* trailingComment = strstr(s, "//");
			if (trailingComment)
				*trailingComment = '\0';
			const NameValuePairs nvp(s);
			const char* name = GetName(&nvp);
			if (name) {
				if (ShaderToyBuffer::Find(name) == nullptr && nvp.size() <= 1 && name[0] != '[')
					printf("warning: pass %u input buffer (\"%s\") has not been defined yet!\n", m_passIndex, name);
				if (allowSpecificInputIndex)
					m_inputs[inputIndex].m_buffer = ShaderToyBuffer::Add(m_passIndex, name, &nvp);
				else {
					m_inputs.resize(m_inputs.size() + 1);
					m_inputs.back().m_buffer = ShaderToyBuffer::Add(m_passIndex, name, &nvp);
				}
			} else
				printf("error: pass %u input description expected to start with name!\n", m_passIndex);
		} else
			printf("error: pass %u input not processed, missing ':'!\n", m_passIndex);
	}

	void AddOutput(char* s)
	{
		uint32 outputIndex = 0;
		if (isdigit(*s)) {
			outputIndex = (uint32)atoi(s);
			while (isdigit(*s))
				s++;
		}
		static uint32 maxDrawBuffers = 0;
		if (maxDrawBuffers == 0)  {
			uint32 maxColorAttachments = 0;
			glGetIntegerv(GL_MAX_COLOR_ATTACHMENTS, (GLint*)&maxColorAttachments);
			printf("GL_MAX_COLOR_ATTACHMENTS = %u\n", maxColorAttachments);
			glGetIntegerv(GL_MAX_DRAW_BUFFERS, (GLint*)&maxDrawBuffers);
			printf("GL_MAX_DRAW_BUFFERS = %u\n", maxDrawBuffers);
		}
		if (outputIndex < maxDrawBuffers) {
			SkipLeadingWhitespace(s);
			if (*s++ == ':') {
				if (m_outputs.size() < outputIndex + 1)
					m_outputs.resize(outputIndex + 1);
				PassOutput& output = m_outputs[outputIndex];
				if (output.m_buffer == nullptr) {
					char* trailingComment = strstr(s, "//");
					if (trailingComment)
						*trailingComment = '\0';
					const NameValuePairs nvp(s);
					const char* name = GetName(&nvp);
					if (name) {
						output.m_buffer = ShaderToyBuffer::Add(m_passIndex, name, &nvp);
						output.m_layerOrSliceIndex = nvp.GetUIntValue("layer");
						output.m_mipIndex = nvp.GetUIntValue("mip");
					} else
						printf("error: pass %u output %u description expected to start with name!\n", m_passIndex, outputIndex);
				} else
					printf("error: pass %u output %u already bound to a buffer!\n", m_passIndex, outputIndex);
			} else
				printf("error: pass %u output %u not processed, missing ':'!\n", m_passIndex, outputIndex);
		} else
			printf("error: pass %u output %u not processed, max output index is %u!\n", m_passIndex, outputIndex, maxDrawBuffers - 1);
	}

#if SUPPORT_IMAGES
	void AddImage(char* s)
	{
		SkipLeadingWhitespace(s);
		if (*s++ == ':') {
			char* trailingComment = strstr(s, "//");
			if (trailingComment)
				*trailingComment = '\0';
			const NameValuePairs nvp(s);
			const char* name = GetName(&nvp);
			if (name) {
				const char* bufferName = nvp.GetStringValue("buffer", name);
				if (ShaderToyBuffer::Find(bufferName) == nullptr && nvp.size() <= 1 && bufferName[0] != '[')
					printf("warning: pass %u image buffer (\"%s\") has not been defined yet!\n", m_passIndex, bufferName);
				ShaderToyBuffer* buffer = ShaderToyBuffer::Add(m_passIndex, bufferName, &nvp);
				m_images.resize(m_images.size() + 1);
				PassImage& image = m_images.back();
				image.m_name = name;
				image.m_buffer = buffer;
				image.m_layered = nvp.GetBoolValue("layered");
				image.m_layerOrSliceIndex = nvp.GetUIntValue("layer");
				image.m_mipIndex = nvp.GetUIntValue("mip");
				if (nvp.HasValue("read") || nvp.HasValue("write")) {
					const bool readAccess = nvp.GetBoolValue("read");
					const bool writeAccess = nvp.GetBoolValue("write");
					if (readAccess && writeAccess)
						image.m_access = GL_READ_WRITE;
					else if (readAccess)
						image.m_access = GL_READ_ONLY;
					else if (writeAccess)
						image.m_access = GL_WRITE_ONLY;
				} else
					image.m_access = GL_READ_WRITE; // default
				image.m_internalFormat = TextureFormatInfo(buffer->m_desc.m_format).m_internalFormat;
			} else
				printf("error: pass %u image description expected to start with name!\n", m_passIndex);
		} else
			printf("error: pass %u image not processed, missing ':'!\n", m_passIndex);
	}
#endif // SUPPORT_IMAGES

	class PassRef
	{
	public:
		PassRef(const char* path, float data = 0.0f) : m_path(path), m_data(data) {}
		std::string m_path;
		float m_data;
	};

	static ShaderToyRenderPass* LoadPass(
		uint32 passIndex,
		const PassRef& ref,
		GLuint commonVertexShaderID,
		const std::vector<std::string>& sourceHeader)
	{
		const char* path = ref.m_path.c_str();
		printf("loading pass \"%s\" ..\n", path);
	#if USE_GUI
		GUISlider::SetCurrentPassIndexForShaderLoad(passIndex);
	#endif // USE_GUI
		ShaderToyRenderPass* pass = nullptr;
		FILE* file = fopen(path, "r");
		if (file) {
			pass = new ShaderToyRenderPass(passIndex, 0);

			// process metadata
			char line[SHADER_CODE_MAX_LINE_SIZE];
			while (rage_fgetline(line, sizeof(line), file)) {
				const std::string lineCopy = line;
				char* s = line;
				SkipLeadingWhitespace(s);
				if (!if_strskip(s, "//"))
					continue;
				SkipLeadingWhitespace(s);
				if      (if_strskip(s, "$BUFFER")) pass->AddBuffer(s);
				else if (if_strskip(s, "$INPUT" )) pass->AddInput(s);
				else if (if_strskip(s, "$OUTPUT")) pass->AddOutput(s);
			#if SUPPORT_IMAGES
				else if (if_strskip(s, "$IMAGE" )) pass->AddImage(s);					
			#endif // SUPPORT_IMAGES
			}
			std::vector<std::string> sourceHeaderPlusInputSamplers;
			sourceHeaderPlusInputSamplers.push_back("");
			sourceHeaderPlusInputSamplers.push_back("//<=== BEGIN SAMPLERS ===>");
			for (uint32 inputIndex = 0; inputIndex < pass->m_inputs.size(); inputIndex++) {
				const PassInput& input = pass->m_inputs[inputIndex];
				const ShaderToyBuffer* buffer = input.m_buffer;
				if (buffer) {
					const std::string samplerTypeStr = GetOpenGLSamplerTypeStr(buffer->m_target, buffer->m_desc.m_format);
					char channelName[256];
					strcpy(channelName, buffer->m_desc.m_name.c_str());
					for (char* s1 = channelName; *s1; s1++) {
						if (!isalnum(*s1))
							*s1 = '_';
					}
					sourceHeaderPlusInputSamplers.push_back(varString("uniform %s iChannel%u;", samplerTypeStr.c_str(), inputIndex));
					sourceHeaderPlusInputSamplers.push_back(varString("#define %s iChannel%u", channelName, inputIndex));
				}
			}
		#if SUPPORT_IMAGES
			for (uint32 imageIndex = 0; imageIndex < pass->m_images.size(); imageIndex++) {
				const PassImage& image = pass->m_images[imageIndex];
				const ShaderToyBuffer* buffer = image.m_buffer;
				if (buffer) {
					const bool isShadow = false;
					const bool isImage = true;
					const std::string samplerTypeStr = GetOpenGLSamplerTypeStr(buffer->m_target, buffer->m_desc.m_format, isShadow, isImage, image.m_layered);
					char imageName[256];
					strcpy(imageName, image.m_name.c_str());
					for (char* s1 = imageName; *s1; s1++) {
						if (!isalnum(*s1))
							*s1 = '_';
					}
					sourceHeaderPlusInputSamplers.push_back(varString("layout(binding=%u,%s) uniform %s iImageChannel%u;", imageIndex, TextureFormatInfo(buffer->m_desc.m_format).m_formatQualifier, samplerTypeStr.c_str(), imageIndex));
					sourceHeaderPlusInputSamplers.push_back(varString("#define %s_IMAGE iImageChannel%u", imageName, imageIndex));
				}
			}
		#endif // SUPPORT_IMAGES
			sourceHeaderPlusInputSamplers.push_back("//<=== END SAMPLERS ===>");
			sourceHeaderPlusInputSamplers.push_back("");
			for (uint32 i = 0; i < sourceHeader.size(); i++)
				sourceHeaderPlusInputSamplers.push_back(sourceHeader[i]);

			const bool alwaysEmit4ComponentVector = true; // pretty sure compiler will optimize out any unused components .. but you can change this if you want
			std::vector<std::string> sourceFooter; // this links the shadertoy main function (mainImage) to the GLSL main function
			std::string params = "";
			sourceFooter.push_back("");
			sourceFooter.push_back("//<=== BEGIN FOOTER ===>");
			if (pass->m_outputs.size() > 0) {
				for (uint32 i = 0; i < pass->m_outputs.size(); i++) {
					const ShaderToyBuffer* buffer = pass->m_outputs[i].m_buffer;
					if (buffer) {
						char layoutStr[64] = "";
						const char* typeStr = TextureFormatInfo(buffer->m_desc.m_format).m_formatType;
						if (alwaysEmit4ComponentVector) {
							if (strcmp(typeStr, "float") == 0 ||
								strcmp(typeStr, "vec2") == 0 ||
								strcmp(typeStr, "vec3") == 0)
								typeStr = "vec4";
							if (strcmp(typeStr, "int") == 0 ||
								strcmp(typeStr, "ivec2") == 0 ||
								strcmp(typeStr, "ivec3") == 0)
								typeStr = "ivec4";
							if (strcmp(typeStr, "uint") == 0 ||
								strcmp(typeStr, "uvec2") == 0 ||
								strcmp(typeStr, "uvec3") == 0)
								typeStr = "uvec4";
						}
						char paramName[64] = "fragOut";
						if (pass->m_outputs.size() > 1) {
							sprintf(layoutStr, "layout(location=%u) ", i);
							strcatf(paramName, "%u", i);
						}
						params += varString("%s%s", params.empty() ? "" : ", ", paramName);
						sourceFooter.push_back(varString("%sout %s %s;", layoutStr, typeStr, paramName));
					}
				}
			} else {
				const char* typeStr = "vec4";
				const char paramName[64] = "fragOut";
				params = paramName;
				sourceFooter.push_back(varString("out %s %s;", typeStr, paramName));
			}
			sourceFooter.push_back("");
			sourceFooter.push_back("void main()");
			sourceFooter.push_back("{");
			sourceFooter.push_back(varString("\tmainImage(%s, gl_FragCoord.xy);", params.c_str()));
			sourceFooter.push_back("}");
			sourceFooter.push_back("//<=== END FOOTER ===>");
			pass->m_programID = LoadShaderProgram(path, commonVertexShaderID, &sourceHeaderPlusInputSamplers, &sourceFooter);
			if (pass->m_programID != 0)
				GetPasses().push_back(pass);
			else {
				delete pass;
				pass = nullptr;
			}
			fclose(file);
		}
		return pass;
	}

	static void LoadShaders(const char* dir = "shaders")
	{
		// this includes all the shadertoy uniforms as well as the common file
		std::vector<std::string> sourceHeader;
		sourceHeader.push_back("//<=== BEGIN HEADER ===>");
		LoadFileIntoStrings(sourceHeader, "shaders_common\\shadertoy_common.h");
		LoadFileIntoStrings(sourceHeader, "shaders_common\\shadertoy_common.glsl");
	#if USE_GUI
		if (g_GUIEnabled) {
			sourceHeader.push_back("#define SLIDER_VAR(type,name,init,rangemin,rangemax) uniform type name = type(init)\n");
			sourceHeader.push_back("#define SLIDER_CHANGED g_GUISliderChanged\n");
			sourceHeader.push_back("uniform bool g_GUISliderChanged = false;\n");
		}
	#endif // USE_GUI
		sourceHeader.push_back("//<=== END HEADER ===>");
		sourceHeader.push_back("");
		sourceHeader.push_back("//<=== BEGIN COMMON ===>");
		std::vector<PassRef> passRefs;
		const uint32 firstLineIndex = (uint32)sourceHeader.size();
		const varString commonPath("%s\\COMMON.glsl", dir);
		LoadFileIntoStrings(sourceHeader, commonPath);
		for (uint32 i = firstLineIndex; i < sourceHeader.size(); i++) { // add buffers specified in common ..
			char temp[SHADER_CODE_MAX_LINE_SIZE];
			strcpy(temp, sourceHeader[i].c_str());
			char* s = temp;
			SkipLeadingWhitespace(s);
			if (!if_strskip(s, "//"))
				continue;
			SkipLeadingWhitespace(s);
			if (if_strskip(s, "$BUFFER")) {
				SkipLeadingWhitespace(s);
				if (*s++ == ':') {
					char* trailingComment = strstr(s, "//");
					if (trailingComment)
						*trailingComment = '\0';
					const NameValuePairs nvp(s);
					const char* name = GetName(&nvp);
					if (name)
						ShaderToyBuffer::Add(-1, name, &nvp);
					else
						printf("error: buffer description expected to start with name!\n");
				} else
					printf("error: buffer not processed, missing ':'!\n");
			} else if (if_strskip(s, "$PASS")) {
				SkipLeadingWhitespace(s);
				if (*s++ == ':') {
					char* trailingComment = strstr(s, "//");
					if (trailingComment)
						*trailingComment = '\0';
					const NameValuePairs nvp(s);
					const char* name = GetName(&nvp);
					if (name) {
						const float data = nvp.GetFloatValue("data");
						passRefs.push_back(PassRef(varString("%s\\%s", dir, name).c_str(), data));
					} else
						printf("error: pass description expected to start with path!\n");
				} else
					printf("error: pass not processed, missing ':'!\n");
			}
		}
	#if USE_GUI
		if (g_GUIEnabled) {
			GUISlider::SetCurrentPassIndexForShaderLoad(-1);
			for (uint32 i = firstLineIndex; i < sourceHeader.size(); i++)
				GUISlider::AddSlider(commonPath, i - firstLineIndex, sourceHeader[i].c_str());
		}
	#endif // USE_GUI
		sourceHeader.push_back("//<=== END COMMON ===>");
		sourceHeader.push_back("");

		if (passRefs.size() == 0) { // no passes specified in COMMON.glsl .. try hardcoded "PASS_0.glsl", "PASS_1.glsl", etc.
			for (uint32 passIndex = 0; passIndex < MAX_PASSES; passIndex++) {
				const varString path("%s\\PASS_%u.glsl", dir, passIndex);
				if (FileExists(path.c_str()))
					passRefs.push_back(PassRef(path.c_str()));
				else
					break;
			}
		}
		// ==================================================================================================
		// TODO -- LoadPass should not store the pass - it should store the *reference* to the pass and only
		// load (and store) the pass if the path is unique. we want to be able to handle multiple refs to the
		// same pass in memory, each ref has its own 'data'
		// ==================================================================================================
		GLuint commonVertexShaderID = 0;
		LoadShader(commonVertexShaderID, "shaders_common\\shadertoy_vertex.glsl", nullptr, GL_VERTEX_SHADER, VERTEX_SHADER_VERSION_STR);
		for (uint32 passIndex = 0; passIndex < passRefs.size(); passIndex++) {
			const ShaderToyRenderPass* pass = LoadPass(passIndex, passRefs[passIndex], commonVertexShaderID, sourceHeader);
			if (pass)
				printf("loaded pass \"%s\"\n", passRefs[passIndex].m_path.c_str());
			else
				printf("failed to load pass \"%s\"!\n", passRefs[passIndex].m_path.c_str());
		}

		if (0) { // dump pass info
			std::vector<ShaderToyRenderPass*>& passes = GetPasses();
			for (uint32 i = 0; i < passes.size(); i++) {
				const ShaderToyRenderPass* pass = passes[i];
				printf("\n");
				for (uint32 j = 0; j < pass->m_inputs.size(); j++) {
					const ShaderToyRenderPass::PassInput& input = pass->m_inputs[j];
					if (input.m_buffer)
						printf("PASS_%u - INPUT%u: %s\n", i, j, input.m_buffer->m_desc.m_name.c_str());
				}
				for (uint32 j = 0; j < pass->m_outputs.size(); j++) {
					const ShaderToyRenderPass::PassOutput& output = pass->m_outputs[j];
					if (output.m_buffer)
						printf("PASS_%u - OUTPUT%u: %s (layer=%u)\n", i, j, output.m_buffer->m_desc.m_name.c_str(), output.m_layerOrSliceIndex);
				}
			}
		}
	}

	static uint32 GetResolution(float res, uint32 resViewport)
	{
		if (res > 0.0f) // explicit resolution
			return (uint32)Ceiling(res);
		else if (res < 0.0f) // proportional to viewport
			return (uint32)Ceiling(-res*(float)resViewport);
		else // viewport
			return resViewport;
	}
	
	void Render()
	{
		if (m_programID != 0) {
		#if SUPPORT_IMAGES
			bool needsImageBarrier = false;
		#endif // SUPPORT_IMAGES
			glUseProgram(m_programID);
			if (m_outputs.size() > 0) {
				if (m_outputFramebufferID == 0) {
					glGenFramebuffers(1, &m_outputFramebufferID);
					glBindFramebuffer(GL_FRAMEBUFFER, m_outputFramebufferID);
					std::vector<GLenum> attachments(m_outputs.size());
					for (uint32 i = 0; i < m_outputs.size(); i++) {
						const PassOutput& output = m_outputs[i];
						attachments[i] = GL_COLOR_ATTACHMENT0 + i;
						if (output.m_buffer) {
							if (output.m_buffer->m_target == GL_TEXTURE_2D_ARRAY ||
								output.m_buffer->m_target == GL_TEXTURE_2D_MULTISAMPLE_ARRAY ||
								output.m_buffer->m_target == GL_TEXTURE_CUBE_MAP ||
								output.m_buffer->m_target == GL_TEXTURE_CUBE_MAP_ARRAY ||
								output.m_buffer->m_target == GL_TEXTURE_3D)
								glFramebufferTextureLayer(GL_FRAMEBUFFER, attachments[i], output.m_buffer->m_textureID, output.m_mipIndex, output.m_layerOrSliceIndex);
							else
								glFramebufferTexture(GL_FRAMEBUFFER, attachments[i], output.m_buffer->m_textureID, output.m_mipIndex);
						} else
							glFramebufferTexture(GL_FRAMEBUFFER, attachments[i], 0, 0);
					}
					glDrawBuffers((GLsizei)m_outputs.size(), attachments.data());
				} else
					glBindFramebuffer(GL_FRAMEBUFFER, m_outputFramebufferID);
				glViewport(0, 0, m_outputs[0].m_buffer->m_res[0], m_outputs[0].m_buffer->m_res[1]);
			} else {
				glBindFramebuffer(GL_FRAMEBUFFER, 0);
				glViewport(0, 0, g_ViewportWidth, g_ViewportHeight);
			}
			const float pixelAspect = 1.0f;
			const Vec3f viewportRes((float)g_ViewportWidth, (float)g_ViewportHeight, pixelAspect);
			const Vec3f outputRes = m_outputs.size() > 0 ? Vec3f((float)m_outputs[0].m_buffer->m_res[0], (float)m_outputs[0].m_buffer->m_res[1], (float)m_outputs[0].m_buffer->m_res[2]) : viewportRes;
			Vec3f channelRes[MAX_INPUTS];
			memset(channelRes, 0, MAX_INPUTS*sizeof(Vec3f));
			for (uint32 inputIndex = 0; inputIndex < m_inputs.size(); inputIndex++) {
				const PassInput& input = m_inputs[inputIndex];
				const ShaderToyBuffer* buffer = input.m_buffer;
				if (buffer) {
					BindTextureTarget(m_programID, buffer->m_textureID, buffer->m_target, inputIndex, varString("iChannel%u", inputIndex));
					const uint32 numLayersOrSlices = buffer->m_desc.m_resolutionZ > 1 ? buffer->m_res[2] : buffer->m_desc.m_numLayers;
					channelRes[inputIndex] = Vec3f((float)buffer->m_res[0], (float)buffer->m_res[1], numLayersOrSlices > 1 ? (float)numLayersOrSlices : pixelAspect);
				}
			}
		#if SUPPORT_IMAGES
			for (uint32 imageIndex = 0; imageIndex < m_images.size(); imageIndex++) {
				const PassImage& image = m_images[imageIndex];
				const ShaderToyBuffer* buffer = image.m_buffer;
				if (buffer) {
					glBindImageTexture(imageIndex, buffer->m_textureID, image.m_mipIndex, image.m_layered ? GL_TRUE : GL_FALSE, image.m_layerOrSliceIndex, image.m_access, image.m_internalFormat);
					if (image.m_access == GL_WRITE_ONLY ||
						image.m_access == GL_READ_WRITE)
						needsImageBarrier = true;
				}
			}
		#endif // SUPPORT_IMAGES
			// TODO -- if this block becomes large, consider changing to a single uniform buffer that can be bound to all shaders
			glUniform3fv(glGetUniformLocation(m_programID, "iResolution"), 1, (const GLfloat*)&viewportRes);
			glUniform3fv(glGetUniformLocation(m_programID, "iOutputResolution"), 1, (const GLfloat*)&outputRes);
			glUniform3fv(glGetUniformLocation(m_programID, "iChannelResolution"), MAX_INPUTS, (const GLfloat*)channelRes);
			glUniform1f(glGetUniformLocation(m_programID, "iTime"), g_Time); 
			glUniform1f(glGetUniformLocation(m_programID, "iTimeDelta"), g_TimeDelta);
			glUniform1i(glGetUniformLocation(m_programID, "iFrame"), (int)g_Frame);
			glUniform1f(glGetUniformLocation(m_programID, "iFrameRate"), 60.0f); // whatev.
			glUniform4f(glGetUniformLocation(m_programID, "iMouse"), (float)g_MouseDragCurr[0], (float)g_MouseDragCurr[1], (float)g_MouseDragStart[0], (float)g_MouseDragStart[1]);
		#if USE_GUI
			GUISlider::SetUniformsForPass(m_passIndex, m_programID);
		#endif // USE_GUI
			glBegin(GL_QUADS);
			glVertex2f(-1.0f, -1.0f);
			glVertex2f(+1.0f, -1.0f);
			glVertex2f(+1.0f, +1.0f);
			glVertex2f(-1.0f, +1.0f);
			glEnd();
		#if SUPPORT_IMAGES
			// TODO -- memory barrier only when needed (i.e. when about to access a buffer via texture or image(read) sampler which was potentially written to earlier)
			if (needsImageBarrier)
				glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
		#endif // SUPPORT_IMAGES
		}
	}

	static void RenderAll()
	{
		std::vector<ShaderToyRenderPass*>& passes = GetPasses();
		ShaderToyBuffer::UpdateAll();
		for (uint32 i = 0; i < passes.size(); i++)
			passes[i]->Render();
		glBindFramebuffer(GL_FRAMEBUFFER, 0); // restore
		glUseProgram(0); // restore
	#if USE_GUI
		g_GUISliderChanged = false;
	#endif // USE_GUI
	}

	static std::vector<ShaderToyRenderPass*>& GetPasses()
	{
		static std::vector<ShaderToyRenderPass*> passes;
		return passes;
	}

	uint32 m_passIndex;
	GLuint m_programID;
	std::vector<PassInput> m_inputs;
	std::vector<PassOutput> m_outputs; // multiple outputs for MRT
#if SUPPORT_IMAGES
	std::vector<PassImage> m_images;
#endif // SUPPORT_IMAGES
	GLuint m_outputFramebufferID;
};

static void DisplayFunc()
{
	g_Keyboard.Update();
#if USE_GUI
	if (g_GUIFrame)
		GUI::Idle(&g_Keyboard);
#endif // USE_GUI

	static bool once = true;
	if (once) {
		once = false;
		ShaderToyRenderPass::LoadShaders(g_ShadersDir.c_str());
	}
	UpdateFrameTime();
	glViewport(0, 0, g_ViewportWidth, g_ViewportHeight);
	glDisable(GL_BLEND);
	ShaderToyRenderPass::RenderAll();

#if USE_GUI
	if (g_GUIFrame) {
		GUI::RenderBegin(0, 0, g_ViewportWidth, g_ViewportHeight);
		GUI::RenderWindow();
	}
#endif // USE_GUI

	g_Frame++;
	glutSwapBuffers();
	glutPostRedisplay();
}

static void MouseFunc(int button, int state, int x, int y)
{
	if (g_GUIFrame && GUI::MouseButton(button, state, x, y, g_Keyboard.GetModifiers()))
		return;

	g_MouseWheelState = 0;
	int buttonID = -1;
	switch (button) {
	case GLUT_LEFT_BUTTON: buttonID = 0; break;
	case GLUT_RIGHT_BUTTON: buttonID = 1; break;
	case GLUT_MIDDLE_BUTTON: buttonID = 2; break;
	}
	if (state == GLUT_DOWN) {
		if (button == 3)
			g_MouseWheelState = +1;
		else if (button == 4)
			g_MouseWheelState = -1;
		else if (buttonID != -1)
			g_MouseButtonState[buttonID] = true;
	} else if (state == GLUT_UP) {
		if (buttonID != -1)
			g_MouseButtonState[buttonID] = false;
	}

	x = Clamp(x, 0, g_ViewportWidth - 1);
	y = Clamp(g_ViewportHeight - 1 - y, 0, g_ViewportHeight - 1);
	if (g_MouseButtonState[0]) {
		if (button == GLUT_LEFT_BUTTON && state == GLUT_DOWN) {
			g_MouseDragStart[0] = x;
			g_MouseDragStart[1] = y;
		}
		g_MouseDragCurr[0] = x;
		g_MouseDragCurr[1] = y;
	} else {
		g_MouseDragStart[0] = 0;
		g_MouseDragStart[1] = 0;
	}
}

static void MotionFunc(int x, int y)
{
	if (g_GUIFrame && GUI::MouseMotion(x, y))
		return;

	if (g_MouseButtonState[0]) {
		g_MouseDragCurr[0] = x;
		g_MouseDragCurr[1] = g_ViewportHeight - 1 - y;
	}
}

static void VisibilityFunc(int vis)
{
}

static void SaveStandardTextures()
{
	// noise
	{
		uint32 w = 256;
		uint32 h = 256;
		Vec4V* image = new Vec4V[w*h];
		for (uint32 i = 0; i < w*h; i++) {
			image[i].xf_ref() = GetRandomValue();
			image[i].yf_ref() = GetRandomValue();
			image[i].zf_ref() = GetRandomValue();
			image[i].wf_ref() = GetRandomValue();
		}
		SaveImage("textures\\RGBA_NOISE_MEDIUM.tga", image, w, h);
		delete[] image;
	}

	// permutation
	{
		typedef uint16 PixelType;
		const uint32 n = 2048;
		PixelType* image = new PixelType[n*n];
		memset(image, 0, n*n*sizeof(PixelType));
		for (uint32 j = 0; j < n; j++) {
			std::vector<PixelType> x(j + 1);
			for (uint32 i = 0; i <= j; i++)
				x[i] = (PixelType)i;
			std::random_shuffle (x.begin(), x.end());
			memcpy(image + j*n, x.data(), (j + 1)*sizeof(PixelType));
		}
		SaveImage("textures\\PERMUTATION.dds", image, n, n, false, true); // save native R16_UNORM
		delete[] image;
	}
}

int main(int argc, const char* argv[])
{
	//SaveStandardTextures();

	StartupMain(argc, argv);
	if (argc > 1)
		g_ShadersDir = argv[1];

	glutInit(&argc, (char**)argv);
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_ALPHA);
	glutInitWindowSize(g_ViewportWidth, g_ViewportHeight);
	glutCreateWindow(argv[0]);
	glutReshapeFunc(ReshapeFunc);
	glutDisplayFunc(DisplayFunc);
	glutMouseFunc(MouseFunc);
	glutMotionFunc(MotionFunc);
	glutPassiveMotionFunc(MotionFunc);
	glutVisibilityFunc(VisibilityFunc);

	const GLubyte* versionStr = glGetString(GL_VERSION);
	fprintf(stdout, "OpenGL version: %s\n", versionStr);
#if FREEGLUT_INCLUDE_GLEW_2_1_0
	const GLenum err = glewInit();
	if (err == GLEW_OK) {
		fprintf(stdout, "glewInit: Status OK - using GLEW_VERSION %s\n", glewGetString(GLEW_VERSION));
		if (GLEW_VERSION_4_4) {
			// ok
		} else {
			fprintf(stderr, "OpenGL version 4.4 required, but not present ..\n");
			system("pause");
			exit(-1);
		}
	} else {
		fprintf(stderr, "glewInit error: %s\n", glewGetErrorString(err));
		system("pause");
		exit(-1);
	}
#endif // FREEGLUT_INCLUDE_GLEW_2_1_0

	if (glewIsSupported("GL_NV_gpu_shader5") &&
		glewIsSupported("GL_NV_bindless_texture")) {
		printf("GPU Shader 5 support enabled\n");
		g_GPUShader5 = true;
	} else {
		printf("GPU Shader 5 support not enabled\n");
		// ===================================================================================
		// TODO -- this code is ineffective - the ReshapeFunc callback gets invoked as soon as
		// glutMainLoop is entered, and the width and height get reset according to the
		// call to glutInitWindowSize above. not sure how to fix this cleanly without hacking
		// the ReshapeFunc callback, so that's what i did.
		// ===================================================================================
		//g_ViewportWidth /= 2;
		//g_ViewportHeight /= 2;
		//glutInitWindowSize(g_ViewportWidth, g_ViewportHeight);
		//glutReshapeWindow(g_ViewportWidth, g_ViewportHeight);
	}

#if 1
	// https://www.khronos.org/opengl/wiki/OpenGL_Error
	class OpenGLDebugMessageCallback { public: static void GLAPIENTRY func(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam) {
		if (severity != GL_DEBUG_SEVERITY_NOTIFICATION &&
			severity != GL_DEBUG_SEVERITY_LOW) {
			const char* sourceStr = "?";
			switch (source) {
			case GL_DEBUG_SOURCE_API:             sourceStr = "API";             break;
			case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   sourceStr = "WINDOW_SYSTEM";   break;
			case GL_DEBUG_SOURCE_SHADER_COMPILER: sourceStr = "SHADER_COMPILER"; break;
			case GL_DEBUG_SOURCE_THIRD_PARTY:     sourceStr = "THIRD_PARTY";     break;
			case GL_DEBUG_SOURCE_APPLICATION:     sourceStr = "APPLICATION";     break;
			case GL_DEBUG_SOURCE_OTHER:           sourceStr = "OTHER";           break;
			}
			const char* typeStr = "?";
			switch (type) {
			case GL_DEBUG_TYPE_ERROR:               typeStr = "ERROR";               break; // An error, typically from the API
			case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: typeStr = "DEPRECATED_BEHAVIOR"; break; // Some behavior marked deprecated has been used
			case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  typeStr = "UNDEFINED_BEHAVIOR";  break; // Something has invoked undefined behavior
			case GL_DEBUG_TYPE_PORTABILITY:         typeStr = "PORTABILITY";         break; // Some functionality the user relies upon is not portable
			case GL_DEBUG_TYPE_PERFORMANCE:         typeStr = "PERFORMANCE";         break; // Code has triggered possible performance issues
			case GL_DEBUG_TYPE_MARKER:              typeStr = "MARKER";              break; // Command stream annotation
			case GL_DEBUG_TYPE_PUSH_GROUP:          typeStr = "PUSH_GROUP";          break; // Group pushing
			case GL_DEBUG_TYPE_POP_GROUP:           typeStr = "POP_GROUP";           break; // Group popping
			case GL_DEBUG_TYPE_OTHER:               typeStr = "OTHER";               break; // Some type that isn't one of these
			}
			const char* severityStr = "?";
			switch (severity) {
			case GL_DEBUG_SEVERITY_HIGH:         severityStr = "HIGH";         break; // All OpenGL Errors, shader compilation/linking errors, or highly-dangerous undefined behavior
			case GL_DEBUG_SEVERITY_MEDIUM:       severityStr = "MEDIUM";       break; // Major performance warnings, shader compilation/linking warnings, or the use of deprecated functionality
			case GL_DEBUG_SEVERITY_LOW:          severityStr = "LOW";          break; // Redundant state change performance warning, or unimportant undefined behavior
			case GL_DEBUG_SEVERITY_NOTIFICATION: severityStr = "NOTIFICATION"; break; // Anything that isn't an error or performance issue.
			}
			if (source == GL_DEBUG_SOURCE_API && // skip this particular warning about vertex shadering being recompiled based on GL state .. i don't understand it
				type == GL_DEBUG_TYPE_PERFORMANCE &&
				severity == GL_DEBUG_SEVERITY_MEDIUM) {
				if (strstr(message, "Program/shader state performance warning: Vertex shader in program") ||
					strstr(message, "shader recompiled due to state change"))
					return;
			}
			if (g_CurrentShaderBeingCompiled)
				fprintf(stderr, "error compiling shader \"%s\"!\n", g_CurrentShaderBeingCompiled);
			fprintf(stderr, "GL CALLBACK:%s (source=%s, type=%s, severity=%s): %s\n",
				type == GL_DEBUG_TYPE_ERROR ? " ** GL ERROR **" : "",
				sourceStr,
				typeStr,
				severityStr,
				message);
			if (severity == GL_DEBUG_SEVERITY_HIGH) {
				static bool stop = true;
				if (stop) {
					stop = false;
					__debugbreak();
				}
			}
		}
	}};
	glEnable(GL_DEBUG_OUTPUT);
	glDebugMessageCallback(OpenGLDebugMessageCallback::func, nullptr);
#endif

	glutMainLoop();
	return 0;
}