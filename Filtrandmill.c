//Header behavior overrides
#define _WINDOWS_
#define SDL_MAIN_HANDLED

//Header includes
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <GL/gl.h>
#include <GL/glext.h>

//OpenGL extension functions
static PFNGLATTACHSHADERPROC             glAttachShader;
static PFNGLBINDBUFFERPROC               glBindBuffer;
static PFNGLBINDVERTEXARRAYPROC          glBindVertexArray;
static PFNGLBUFFERDATAPROC               glBufferData;
static PFNGLCOMPILESHADERPROC            glCompileShader;
static PFNGLCREATEPROGRAMPROC            glCreateProgram;
static PFNGLCREATESHADERPROC             glCreateShader;
static PFNGLDELETEBUFFERSPROC            glDeleteBuffers;
static PFNGLDELETEPROGRAMPROC            glDeleteProgram;
static PFNGLDELETESHADERPROC             glDeleteShader;
static PFNGLDELETEVERTEXARRAYSPROC       glDeleteVertexArrays;
static PFNGLDETACHSHADERPROC             glDetachShader;
static PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray;
static PFNGLENABLEVERTEXATTRIBARRAYPROC  glEnableVertexAttribArray;
static PFNGLGENBUFFERSPROC               glGenBuffers;
static PFNGLGENVERTEXARRAYSPROC          glGenVertexArrays;
static PFNGLGETATTRIBLOCATIONPROC        glGetAttribLocation;
static PFNGLGETPROGRAMIVPROC             glGetProgramiv;
static PFNGLGETSHADERINFOLOGPROC         glGetShaderInfoLog;
static PFNGLGETSHADERIVPROC              glGetShaderiv;
static PFNGLGETUNIFORMLOCATIONPROC       glGetUniformLocation;
static PFNGLLINKPROGRAMPROC              glLinkProgram;
static PFNGLSHADERSOURCEPROC             glShaderSource;
static PFNGLUNIFORMMATRIX4FVPROC         glUniformMatrix4fv;
static PFNGLUNIFORM1FPROC                glUniform1f;
static PFNGLUNIFORM2FVPROC               glUniform2fv;
static PFNGLUNIFORM3FVPROC               glUniform3fv;
static PFNGLUNIFORM4FVPROC               glUniform4fv;
static PFNGLUSEPROGRAMPROC               glUseProgram;
static PFNGLVERTEXATTRIBPOINTERPROC      glVertexAttribPointer;

static PFNGLGENFRAMEBUFFERSPROC          glGenFramebuffers;
static PFNGLBINDFRAMEBUFFERPROC          glBindFramebuffer;
static PFNGLFRAMEBUFFERTEXTUREPROC       glFramebufferTexture;
static PFNGLFRAMEBUFFERTEXTURE2DPROC     glFramebufferTexture2D;
static PFNGLDRAWBUFFERSPROC              glDrawBuffers;
static PFNGLCHECKFRAMEBUFFERSTATUSPROC   glCheckFramebufferStatus;
static PFNGLDELETEFRAMEBUFFERSPROC       glDeleteFramebuffers;

//Mathematical constants
#define PI  3.1415927f
#define PI2 6.2831853f
#define TRUE -1
#define FALSE 0

//GUI parameters
#define SCROLL_STOP_THRESHOLD 0.01f
#define SCROLL_PER_ROW 300.0f
#define IMAGES_PER_ROW 4
#define ROWS_IN_MEMORY 5

//Width and height of the sample taken for estimating normalization
#define NORMALIZATION_SAMPLE_SIZE 4

//Textures reserved for specific, non-display purposes
#define RESERVED_TEXTURES 2
//Maximum images that may be in memory at one time
#define MAX_TEXTURES (IMAGES_PER_ROW * ROWS_IN_MEMORY) + RESERVED_TEXTURES

//Expression generation macros
//TODO: EXP_RANDOM_CHANNEL should be (0x10 | randomi(9)) when all 9 channels I decided upon are included, maybe
#define EXP_RANDOM_OPERATOR (rand() & 0x7)
#define EXP_RANDOM_CHANNEL (0x10 | randomi(3))
#define EXP_RANDOM_CONSTANT (0x20 | (rand() & 0xF))
#define EXP_RANDOM_CHANNEL_OR_CONSTANT (rand() & 1 ? EXP_RANDOM_CHANNEL : EXP_RANDOM_CONSTANT)

//The three shaders: the only vertex shader, one fragment shader that is only compiled once, and one that gets modified and compiled for each new image
static const char soleVertexShader[] = "#version 330\n"
"uniform mat4 projection;"
"uniform vec2 translation;"
"uniform float size;"
"layout(location = 0) in vec2 position;"
"layout(location = 1) in vec2 vertexUV;"
"out vec2 UV;"
"void main() {"
"    gl_Position = projection * vec4(size * position + translation,0,1);"
"    UV = vertexUV;"
"}"
;
//fragmentShaderTemplate will hold the template and the to-be-compiled component. Elements 0 and 2 are template components, while element 1 can be modified.
static char* fragmentShaderTemplate[3] = {"#version 330\n uniform sampler2D t; uniform vec3 normalizeMult; uniform vec3 normalizeAdd; in vec2 UV; layout(location = 0) out vec3 color; void main() {color = ", NULL, ";}"};

//Shader info log
static char LOG[1024 * 8];

/*****************************************************************************
 *                                   Types                                   *
 *****************************************************************************/

//Main program state
typedef struct {

    //SDL fields
    SDL_GLContext  gl;     //Handle to OpenGL rendering context
    SDL_Window    *window; //Handle to application window

    //OpenGL fields
    GLuint program;   //Shader program
    GLuint svertex;   //Vertex shader
    GLuint sfragment; //Fragment shader

	GLuint attrib_position;
	GLuint attrib_projection;
	GLuint attrib_translation;
	GLuint attrib_vertexUV;
    GLuint attrib_texture;
	GLuint attrib_size;

    GLuint textures[MAX_TEXTURES];
    GLuint rttFramebuffer; //Render-to-texture framebuffer
    int updated; //Determines whether we need to render
    int usedTextures;

    //Application fields
    int width;  //Viewport width in pixels
    int height; //Viewport height in pixels
	int oldCursorX; //Cursor coordinates
	int oldCursorY;
	int buttonDown; //Mouse button being pressed (0 = none)

	int inputImageSize;

    //Scene fields
    GLuint VAB; //Vertex array buffer
	GLuint VAO; //Vertex array object

	//Animation variables
	unsigned long int scrollMajor; //Number of rows scrolled
	float scrollMinor; //Either percent or pixels scrolled between two rows; haven't decided yet
	float scrollVelocity;
} APP;

typedef struct {
    GLuint tex; //The textures[] array won't change, so it's fine to just copy a value from that array to this field. I could make this match the index of the texturse[] entry, but that's unnecessary processing time.
    char *eR; //The bytes that represent operators and operands in the expression for the red channel
    char *eG; //The bytes that represent operators and operands in the expression for the green channel
    char *eB; //The bytes that represent operators and operands in the expression for the blue channel
    //Do I need a hash set of all the expressions so you can hunt for matches? It'd probably be best to do something like that that can take a O(n) search time down to O(log n), but it's best to keep insertion time below O(n) too.
    //TODO: Should I just make eG and EB match eR but with R->G->B->R / Chroma->Luminance->Chroma / Hue->Saturation->Value->Hue rotations?
} GeneratedImage;



/*****************************************************************************
 *                             Utility Functions                             *
 *****************************************************************************/

//Load a file entirely into memory
static uint8_t* LoadFile(char *filename, uint32_t *size) {
    uint8_t *data = NULL; //File buffer
    FILE    *file = NULL; //File handle

    //Open the file
    file = fopen(filename, "rb");
    if (!file) goto catch;

    //Check that the file size is not too large
    fseek(file, 0, SEEK_END);
    if (*size && (uint32_t) ftell(file) > *size)
        goto catch;
    *size = ftell(file);
    fseek(file, 0, SEEK_SET);

    //Load file data into memory
    data = malloc(*size);
    if (!data) goto catch;
    if (fread(data, 1, *size, file) != *size)
        goto catch;

    //Close the file
    fclose(file);
    return data;

//Error handling
catch:
    if (file) fclose(file);
    if (data) free(data);
    return NULL;
}

//Generates a random number in the range of [0.0, 1.0)
static float random() {
    return (float) rand() / ((float) RAND_MAX + 1.0f);
}

//Generates a random integer in the range of [0, count)
static int randomi(int count) {
    return (int) floorf(random() * count);
}

//These two functions depend on app->height, which is variable, so they're inline functions and not macros.
static inline int rowsPerScreen(APP *app) {
    return (app->height - 1) / SCROLL_PER_ROW + 1;
}

static inline int imagesPerScreen(APP *app) {
    return rowsPerScreen(app) * IMAGES_PER_ROW;
}

/*****************************************************************************
 *                      Initializers and Uninitializers                      *
 *****************************************************************************/

//Initialize application data
static void InitApp(APP *app) {
	app->usedTextures = RESERVED_TEXTURES;
	app->scrollMajor = 0;
	app->scrollMinor = 0.0f;
	app->buttonDown = 0;
	app->oldCursorX = 0; app->oldCursorY = 0;
}

//Uninitialize application data
static void UninitApp(APP *app) {

}

//Render to app->textures[textureIdx] using the program currently in fragmentShaderTemplate. Expects you to put something in fragmentShaderTemplate[1] before calling it.
static void RenderToTexture(APP *app, int textureIdx) {
    GLint   status;
    GLsizei length;
	float vector[2];
    float matrix[16];
	float normalizeMult[3] = {1.0f, 1.0f, 1.0f};
	float normalizeAdd[3] = {0.0f, 0.0f, 0.0f};

	glBindFramebuffer(GL_FRAMEBUFFER, app->rttFramebuffer);
    glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, app->textures[1], 0); //Use a reserved texture, which is of type RGB16F, for finding normalization parameters

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "Framebuffer setup failed; status = %d\r\n", glCheckFramebufferStatus(GL_FRAMEBUFFER));
        goto catch;
    }

    //Set the viewport for the framebuffer
    glViewport(0,0,256,256);

    //Prepare basic fragment shader
    GLuint tempShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(tempShader, 3, (const GLchar **)&fragmentShaderTemplate[0], NULL);
    glCompileShader(tempShader);
    glGetShaderiv(tempShader, GL_COMPILE_STATUS, &status);
    if (tempShader == 0 || status != GL_TRUE) {
        fprintf(stderr, "Could not create fragment shader.\r\n");

        //Output shader info log
        glGetShaderInfoLog(tempShader, sizeof LOG, &length,
            (GLchar *) &LOG[0]);
        fprintf(stderr, "%s\r\n", LOG);
        goto catch;
    }

    //Prepare shader program
    GLuint tempProgram = glCreateProgram();
    glAttachShader(tempProgram, app->svertex);
    glAttachShader(tempProgram, tempShader);
    glLinkProgram(tempProgram);
    glGetProgramiv(tempProgram, GL_LINK_STATUS, &status);
    if (tempProgram == 0 || status != GL_TRUE) {
        fprintf(stderr, "Could not create shader program.\r\n");
        goto catch;
    }
    glUseProgram(tempProgram);

	GLuint attrib_position, attrib_projection, attrib_translation, attrib_vertexUV, attrib_texture, attrib_size, attrib_nm, attrib_na;

	//Get shader attrib locations
	attrib_position = glGetAttribLocation(tempProgram, "position");
	attrib_vertexUV = glGetAttribLocation(tempProgram, "vertexUV");
	//Uniforms
	attrib_projection = glGetUniformLocation(tempProgram, "projection");
	attrib_translation = glGetUniformLocation(tempProgram, "translation");
	attrib_texture = glGetUniformLocation(tempProgram, "t");
    attrib_size = glGetUniformLocation(tempProgram, "size");
	attrib_nm = glGetUniformLocation(tempProgram, "normalizeMult"); //Value to multiply by for (linear) normalization
    attrib_na = glGetUniformLocation(tempProgram, "normalizeAdd"); //Value to add for (linear) normalization

    //Set some uniforms
    glUniform3fv(attrib_nm, 1, normalizeMult);
    glUniform3fv(attrib_na, 1, normalizeAdd);

    //Change the projection matrix
    memset(matrix, 0, sizeof matrix);
    matrix[0] = 2.0f / app->inputImageSize;
    matrix[5] = 2.0f / app->inputImageSize;
	matrix[12] = -1;
	matrix[13] = -1;
    matrix[15] = 1;
    glUniformMatrix4fv(attrib_projection, 1, GL_FALSE, matrix);

    //glClear(GL_COLOR_BUFFER_BIT); //It's really not necessary to clear the color data since we're gonna overwrite it all anyway.

    glBindTexture(GL_TEXTURE_2D, app->textures[0]);
    glUniform1f(attrib_texture, app->textures[0]);

    //Position the image at 0,0
    vector[0] = 0.0f;
    vector[1] = 0.0f;
    glUniform2fv(attrib_translation, 1, vector);

    //We'll draw the image smaller for this normalization parameter estimation stage
    glUniform1f(attrib_size, (float)NORMALIZATION_SAMPLE_SIZE);

    //Draw the image (but small!)
    glBindVertexArray(app->VAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    //Now, using the results of our test run, figure out what normalizeMult and normalizeAdd should be, using glReadPixels.
    //Make a buffer to hold 3 channels of floats, taken from the initial rendered image in order to estimate the normalization parameters for the generated formula.
    float pixelBuffer[NORMALIZATION_SAMPLE_SIZE * NORMALIZATION_SAMPLE_SIZE * 3];

    glPixelStorei(GL_PACK_ALIGNMENT, 1); //Tell OpenGL not to align the output of glReadPixels to 4-byte boundaries.
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, NORMALIZATION_SAMPLE_SIZE, NORMALIZATION_SAMPLE_SIZE, GL_RGB, GL_FLOAT, &pixelBuffer[0]);

    //Find the min and max for each channel
    float min[3] = {__FLT_MAX__, __FLT_MAX__, __FLT_MAX__};
    float max[3] = {-__FLT_MAX__, -__FLT_MAX__, -__FLT_MAX__};
    for (int x = 0; x < NORMALIZATION_SAMPLE_SIZE * NORMALIZATION_SAMPLE_SIZE * 3; x += 3) {
        if (pixelBuffer[x] < min[0]) min[0] = pixelBuffer[x];
        if (pixelBuffer[x] > max[0]) max[0] = pixelBuffer[x];

        if (pixelBuffer[x+1] < min[1]) min[1] = pixelBuffer[x+1];
        if (pixelBuffer[x+1] > max[1]) max[1] = pixelBuffer[x+1];

        if (pixelBuffer[x+2] < min[2]) min[2] = pixelBuffer[x+2];
        if (pixelBuffer[x+2] > max[2]) max[2] = pixelBuffer[x+2];
    }

    //Now convert min and max to multiplication and addition parameters.
    for (int y = 0; y < 3; y++) {
        if (min[y] == max[y]) {
            //Set multiplication to 1.0 and addition to minimum value's negative, so this channel is all 0.
            normalizeAdd[y] = -min[y];
            normalizeMult[y] = 1.0f;
        } else {
            normalizeMult[y] = 1.0f / (max[y] - min[y]); //Divide by the range of the values, in other words.
            normalizeAdd[y] = -min[y] * normalizeMult[y]; //And subtract the new minimum value (since multiplication comes before addition in the shader code I chose)
        }
    }
    glUniform3fv(attrib_nm, 1, normalizeMult);
    glUniform3fv(attrib_na, 1, normalizeAdd);

    //TODO: We can now apply the normalization to the contents of pixelBuffer[] and use that as a key to check for expression equivalence.

    //Finally, process the whole image and apply the normalization parameters simultaneously, putting the results in a standard GL_RGB texture.
    //Set up an image-specific texture for render-to-texture stuff
	glBindTexture(GL_TEXTURE_2D, app->textures[textureIdx]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, app->inputImageSize, app->inputImageSize, 0, GL_RGB, GL_UNSIGNED_BYTE, 0);

	glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, app->textures[textureIdx], 0); //Use the image-specific output texture for output this time
	glUniform1f(attrib_size, (float)app->inputImageSize); //Set the proper image size
    //Do roughly the same output, but this time, it'll apply our normalization parameters.
    glBindTexture(GL_TEXTURE_2D, app->textures[0]);
    glUniform1f(attrib_texture, app->textures[0]);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);


catch:
    //Return to using the normal shader
    glUseProgram(app->program);

    //Cleanup
    glDetachShader(tempProgram, app->svertex);
    glDetachShader(tempProgram, tempShader);
    glDeleteShader(tempShader);
    glDeleteProgram(tempProgram);

    glBindFramebuffer(GL_FRAMEBUFFER, 0); //Draw to screen again after this, not to a framebuffer
    glViewport(0, 0, app->width, app->height);
}

static void GenerateRect(APP *app) {
    struct {
        float x;
        float y;
    } attribs[] = {
		{ 1.0f, 0.0f },
		{ 1.0f, 1.0f },
		{ 0.0f, 0.0f },
		{ 0.0f, 1.0f }
    };

    glGenBuffers(1, &app->VAB);
    glBindBuffer(GL_ARRAY_BUFFER, app->VAB);
    glBufferData(GL_ARRAY_BUFFER, sizeof attribs, attribs, GL_STATIC_DRAW);
	glGenVertexArrays(1, &app->VAO);
	glBindVertexArray(app->VAO);

    glEnableVertexAttribArray(app->attrib_position);
    glVertexAttribPointer(app->attrib_position, 2, GL_FLOAT, GL_FALSE, 8, (void *) 0);

    //UV == rectangle coordinates
    glEnableVertexAttribArray(app->attrib_vertexUV);
    glVertexAttribPointer(app->attrib_vertexUV, 2, GL_FLOAT, GL_FALSE, 8, (void *) 0);
}

//Initialize OpenGL
static int InitGL(APP *app) {
    GLint   status;
    GLsizei length;

    //Refer to shader sources
    const GLchar *svertex   = (const GLchar *) &soleVertexShader[0];
    const GLchar **sfragment = (const GLchar **) &fragmentShaderTemplate[0];

    //Resolve extension function addresses
    #define GLEXT(x) ((*(void **)&x=(void*)SDL_GL_GetProcAddress(#x))==NULL)
    if (
        GLEXT(glAttachShader            ) ||
        GLEXT(glBindBuffer              ) ||
        GLEXT(glBindVertexArray         ) ||
        GLEXT(glBufferData              ) ||
        GLEXT(glCompileShader           ) ||
        GLEXT(glCreateProgram           ) ||
        GLEXT(glCreateShader            ) ||
        GLEXT(glDeleteBuffers           ) ||
        GLEXT(glDeleteProgram           ) ||
        GLEXT(glDeleteShader            ) ||
        GLEXT(glDeleteVertexArrays      ) ||
        GLEXT(glDetachShader            ) ||
        GLEXT(glDisableVertexAttribArray) ||
        GLEXT(glEnableVertexAttribArray ) ||
        GLEXT(glGenBuffers              ) ||
        GLEXT(glGenVertexArrays         ) ||
        GLEXT(glGetAttribLocation       ) ||
        GLEXT(glGetProgramiv            ) ||
        GLEXT(glGetShaderInfoLog        ) ||
        GLEXT(glGetShaderiv             ) ||
        GLEXT(glGetUniformLocation      ) ||
        GLEXT(glLinkProgram             ) ||
        GLEXT(glShaderSource            ) ||
        GLEXT(glUniformMatrix4fv        ) ||
        GLEXT(glUniform1f               ) ||
        GLEXT(glUniform2fv              ) ||
        GLEXT(glUniform3fv              ) ||
        GLEXT(glUniform4fv              ) ||
        GLEXT(glUseProgram              ) ||
        GLEXT(glGenFramebuffers         ) ||
        GLEXT(glBindFramebuffer         ) ||
        GLEXT(glFramebufferTexture      ) ||
        GLEXT(glFramebufferTexture2D    ) ||
        GLEXT(glDrawBuffers             ) ||
        GLEXT(glCheckFramebufferStatus  ) ||
        GLEXT(glDeleteFramebuffers      ) ||
        GLEXT(glVertexAttribPointer     )
    ) {
        fprintf(stderr, "Error initializing OpenGL extensions.\r\n");
        goto catch;
    }
    #undef GLEXT

    //Initialize some OpenGL state
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	//Activate backface culling
	glCullFace(GL_BACK);
	glEnable(GL_CULL_FACE);

    //Prepare vertex shader
    app->svertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(app->svertex, 1, &svertex, NULL);
    glCompileShader(app->svertex);
    glGetShaderiv(app->svertex, GL_COMPILE_STATUS, &status);
    if (app->svertex == 0 || status != GL_TRUE) {
        fprintf(stderr, "Could not create vertex shader.\r\n");

        //Output shader info log
        glGetShaderInfoLog(app->svertex, sizeof LOG, &length,
            (GLchar *) &LOG[0]);
        fprintf(stderr, "%s\r\n", LOG);

        goto catch;
    }

    //Prepare basic fragment shader
    fragmentShaderTemplate[1] = "texture(t, UV).rgb";
    //fragmentShaderTemplate[1] = "vec3(1,1,1)";
    app->sfragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(app->sfragment, 3, sfragment, NULL);
    glCompileShader(app->sfragment);
    glGetShaderiv(app->sfragment, GL_COMPILE_STATUS, &status);
    if (app->sfragment == 0 || status != GL_TRUE) {
        fprintf(stderr, "Could not create fragment shader.\r\n");

        //Output shader info log
        glGetShaderInfoLog(app->sfragment, sizeof LOG, &length,
            (GLchar *) &LOG[0]);
        fprintf(stderr, "%s\r\n", LOG);

        goto catch;
    }

    //Prepare shader program
    app->program = glCreateProgram();
    glAttachShader(app->program, app->svertex);
    glAttachShader(app->program, app->sfragment);
    glLinkProgram(app->program);
    glGetProgramiv(app->program, GL_LINK_STATUS, &status);
    if (app->program == 0 || status != GL_TRUE) {
        fprintf(stderr, "Could not create shader program.\r\n");
        goto catch;
    }
    glUseProgram(app->program);

	//Get shader attrib locations
	app->attrib_position = glGetAttribLocation(app->program, "position");
	app->attrib_vertexUV = glGetAttribLocation(app->program, "vertexUV");
	//Uniforms
	app->attrib_projection = glGetUniformLocation(app->program, "projection");
	app->attrib_translation = glGetUniformLocation(app->program, "translation");
	app->attrib_texture = glGetUniformLocation(app->program, "t");
    app->attrib_size = glGetUniformLocation(app->program, "size");

	//Generate texture
	glGenTextures(MAX_TEXTURES, app->textures);

	//Load texture
	SDL_Surface *tex;
	if ((tex = SDL_LoadBMP("test.bmp"))) {
        glBindTexture(GL_TEXTURE_2D, app->textures[0]);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, tex->w, tex->h, 0, GL_BGR, GL_UNSIGNED_BYTE, tex->pixels);
        glUniform1f(app->attrib_size, (float)tex->w); //Draw the image at full size rather than 1x1 due to reusing the same struct for the vertices and UV coordinates
        app->inputImageSize = tex->w;
        SDL_FreeSurface(tex);
	} else {
	    fprintf(stderr, "Could not load texture.\r\n");
        goto catch;
	}

    GenerateRect(app);

	//Prepare for render-to-texture
	glGenFramebuffers(1, &app->rttFramebuffer);
    GLenum DrawBuffers[1] = {GL_COLOR_ATTACHMENT0};
    glDrawBuffers(1, DrawBuffers);

    //Set up a small texture that we can use for estimating the normalization parameters for a generated image
	glBindTexture(GL_TEXTURE_2D, app->textures[1]);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    //Key point: RGB16F and GL_FLOAT. It's a texture of floats because we'll use it for finding the normalization parameters, and the values won't be clamped to [0,1].
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, NORMALIZATION_SAMPLE_SIZE, NORMALIZATION_SAMPLE_SIZE, 0, GL_RGB, GL_FLOAT, 0);

    return 0;

catch:
    return 1;
}

//Uninitialize OpenGL
static int UninitGL(APP *app) {
    if (app->program != 0) {
        glUseProgram(0);

        glDeleteFramebuffers(1, &app->rttFramebuffer);

        glDeleteTextures(MAX_TEXTURES, app->textures);
		glDeleteBuffers(1, &app->VAB);
		glDeleteVertexArrays(1, &app->VAO);
        glDetachShader(app->program, app->svertex);
        glDetachShader(app->program, app->sfragment);
        glDeleteShader(app->svertex);
        glDeleteShader(app->sfragment);
        glDeleteProgram(app->program);
    }
    return 0;
}

//Initialize SDL and create a window
static int InitSDL(APP *app) {

    //Initial values
    app->gl = NULL;

    //Initialize the SDL library
    SDL_Init(SDL_INIT_VIDEO);

    app->width = 1280;
    app->height = 900;

    //Create a system window
    app->window = SDL_CreateWindow(
        "Filtrandmill",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        app->width,
        app->height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE
    );

    //Error checking
    if (!app->window) {
        fprintf(stderr, "Could not create window: %s\r\n", SDL_GetError());
        goto catch;
    }

    //Create an OpenGL rendering context
    app->gl = SDL_GL_CreateContext(app->window);
    if (!app->gl) {
        fprintf(stderr, "Could not create OpenGL rendering context.\r\n");
        goto catch;
    }
    SDL_GL_MakeCurrent(app->window, app->gl);

    return 0;

//Error handling
catch:
    if (app->gl) SDL_GL_DeleteContext(app->gl);
    return 1;
}

//Uninitialize SDL and delete the window
static void UninitSDL(APP *app) {
    if (app->gl) {
        SDL_GL_DeleteContext(app->gl);
    }
    if (app->window)
        SDL_DestroyWindow(app->window);
    SDL_Quit();
}



/*****************************************************************************
 *                            Animation Functions                            *
 *****************************************************************************/

int expressionLengthLookup[255] = {3,3,3,3,16,10,6,5,   0,0,0,0,0,0,0,0, //Operators, unused operators
                                    15,15,15,   0,0,0,0,0,0,   0,0,0,0,0,0,0, //Channels, unimplemented channels, unused channels
                                    3,3,3,3,3,3,1,2,    5,5,5,5,5,5,3,4, //Positive constants, negative constants
                                    };
//These only exist for operators
char *expressionLeftStringLookup[255] = {
    "(", "(", "(", "(", "pow(abs(", "log(abs(", "mod(", "sin(",     NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, //(+) (-) (*) (/) pow(abs(),abs()) log(abs()) mod(,) sin()    and unused operators
};
char *expressionMiddleStringLookup[255] = {
    "+", "-", "*", "/", "),abs(", NULL, ",", NULL,     NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, //(+) (-) (*) (/) pow(abs(),abs()) log(abs()) mod(,) sin()    and unused operators
};
//This exists for both operators and operands
char *expressionRightStringLookup[255] = {
    ")", ")", ")", ")", "))", "))", ")", ")",     NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, //(+) (-) (*) (/) pow(abs(),abs()) log(abs()) mod(,) sin()    and unused operators
    "texture(t,UV).r", "texture(t,UV).g", "texture(t,UV).b",    NULL, NULL, NULL, NULL, NULL, NULL,     NULL, NULL, NULL, NULL, NULL, NULL, NULL,  //Red, green, blue, unimplemented channels, and unused channels
    "0.1", "0.3", "0.7", "0.9", "1.5", "2.5", "6", "10",    " -0.1", " -0.3", " -0.7", " -0.9", " -1.5", " -2.5", " -6", " -10", //Positive constants, negative constants
};

static inline void putLeft(char* src, char* dest, int* pos) {
    int len = strlen(src);
    memcpy(dest + *pos, src, len); //Destination, source, bytes
    *pos += len;
}

static inline void putRight(char* src, char* dest, int* pos) {
    int len = strlen(src);
    memcpy(dest + *pos - len + 1, src, len); //Destination, source, bytes
    *pos -= len;
}

//Note: the value returned from this function is DYNAMICALLY ALLOCATED, so make very sure that you free it when you're done with it!
static char* expressionToGLSLString(unsigned char *expression, int expressionLength) {
    //First, calculate the size of the buffer we need. We may end up with a little bit too much space (due to unary operators that are given two operands) but never too little.
    int memoryRequirement = 0; //TODO: I actually want all of "normalizeMult*vec3(,,)+normalizeAdd\0" in the returned string.
    for (int x = 0; x < expressionLength; x++) {
        memoryRequirement += expressionLengthLookup[expression[x]];
    }
    memoryRequirement += 1 + 21 + 16 + 4; //null terminator, normalizeMult part, normalizeAdd part, and the ",1,1" I'm testing with

    //Now we know roughly how long the string has to be, so we can allocate it.
    char *buildAString = (char*)malloc(memoryRequirement);
    //Since the second operand is always something basic rather than an expression tree, you can put the first part, i.e. "pow(abs(", at the beginning of buildAString, then
    //you can put the rest of it, i.e. "),abs(myOperand))" at the end, then move to the previous operator. Then fill in the gap between leftPos and rightPos via memmove().
    //  So if I had r3+2%8l (let's say that means log ((red + 3) modulo 2), and the 8 is discarded because this log is unary), it would start with "log(" at the left and ")\0" at the right, then it'd become "log(mod(" at the left of the buffer and ",2))\0" at the right, then for the next step it would become "log(mod(texture(t,UV).r+" at the left and "3,2))\0" at the right, with one unknown character in between them. Then it moves the "3,2))\0" substring so that it begins where that unknown character was, making "log(mod(texture(t,UV).r+3,2))\0\0".
    int leftPos = 0;
    int rightPos = memoryRequirement - 2; //Index memoryRequirement is out of bounds, index memoryRequirement - 1 is the null terminator, and index memoryRequirement - 2 is the last char.
    buildAString[rightPos + 1] = 0;
    //For the time being, PUT_LEFT and PUT_RIGHT are just aliases for putLeft() and putRight().
    #define PUT_LEFT(txt) putLeft(txt,buildAString,&leftPos)
    #define PUT_RIGHT(txt) putRight(txt,buildAString,&rightPos)

    PUT_LEFT("normalizeMult * vec3(");
    PUT_RIGHT(") + normalizeAdd");
    PUT_RIGHT(",1,1"); //TODO: Only temporarily setting green and blue components to 1. I want to cycle the channels that appear in the expression and evaluate it 3 times, basically.

    for (int x = expressionLength - 1; x > 0; x -= 2) {
        switch(expression[x]) {
            //Binary operators
            case 0: case 1: case 2: case 3: case 4: case 6:
                PUT_LEFT(expressionLeftStringLookup[expression[x]]); //Put the operator's initial part on the left
                PUT_RIGHT(expressionRightStringLookup[expression[x]]); //Put the operator's final part on the right
                PUT_RIGHT(expressionRightStringLookup[expression[x-1]]); //Get the second operand and put it at the right. Operands should only be listed in expressionRightStringLookup since they go on the right in this architecture.
                PUT_RIGHT(expressionMiddleStringLookup[expression[x]]); //Put the operator's middle component on the right
                break;
            //Unary operators (log and sine). Unary operators are to only use expressionLeftStringLookup and expressionRightStringLookup, not Middle.
            case 5: case 7:
                PUT_LEFT(expressionLeftStringLookup[expression[x]]); //Put the operator's initial part on the left
                PUT_RIGHT(expressionRightStringLookup[expression[x]]); //Put the operator's final part on the right
                break;

            default: break;
        }
    }
    //TODO: After that loop is done, still need to put the last operand on the left or right, then move the right data to be adjacent to the left data.
    //Putting something on the right and putting it on the left are equivalent when you reach the final operand, so just put it at the left.
    PUT_LEFT(expressionRightStringLookup[expression[0]]);

    memmove(buildAString + leftPos, buildAString + rightPos + 1, memoryRequirement - 1 - rightPos);

    fprintf(stderr, "%s\r\n", buildAString);

    return buildAString;
}

static void GenerateNewImage(APP *app) {
    //TODO: Step 1: make a random expression, starting with operand+operand+operator (1 byte each), replacing a random operand with an operator until you're satisfied, and compare it to all existing ones.
    //TODO: Step 2: convert the bytes into strings, which you can pass directly to RenderToTexture.

    if (app->usedTextures >= MAX_TEXTURES) return; //Error check

    switch (app->usedTextures - RESERVED_TEXTURES) {
        case 0: fragmentShaderTemplate[1] = "normalizeMult * vec3(texture(t, UV).rg, 1) + normalizeAdd"; break;
        case 1: fragmentShaderTemplate[1] = "normalizeMult * vec3(texture(t, UV).r, 1, texture(t, UV).b) + normalizeAdd"; break;
        case 2: fragmentShaderTemplate[1] = "normalizeMult * vec3(1, texture(t, UV).gb) + normalizeAdd"; break;
        case 3: fragmentShaderTemplate[1] = "normalizeMult * texture(t, UV).bgr + normalizeAdd"; break;
        case 4: fragmentShaderTemplate[1] = "normalizeMult * texture(t, UV).rrr + normalizeAdd"; break;
        case 5: fragmentShaderTemplate[1] = "normalizeMult * texture(t, UV).ggg + normalizeAdd"; break;
        case 6: fragmentShaderTemplate[1] = "normalizeMult * texture(t, UV).bbb + normalizeAdd"; break;
        case 7: fragmentShaderTemplate[1] = "normalizeMult * texture(t, UV).gbr + normalizeAdd"; break;
        case 8: fragmentShaderTemplate[1] = "normalizeMult * texture(t, UV).brg + normalizeAdd"; break;
        case 9: fragmentShaderTemplate[1] = "normalizeMult * texture(t, UV).grb + normalizeAdd"; break;
        case 10: fragmentShaderTemplate[1] = "normalizeMult * texture(t, UV).rbr + normalizeAdd"; break;
        case 11: fragmentShaderTemplate[1] = "normalizeMult * texture(t, UV).gbg + normalizeAdd"; break;
        case 12: fragmentShaderTemplate[1] = "normalizeMult * texture(t, UV).brb + normalizeAdd"; break;
        case 13: fragmentShaderTemplate[1] = "normalizeMult * texture(t, UV).bgb + normalizeAdd"; break;
        case 14: fragmentShaderTemplate[1] = "normalizeMult * trunc(texture(t, UV).rgb*8) + normalizeAdd"; break;
        case 15: fragmentShaderTemplate[1] = "normalizeMult * log(texture(t, UV).rgb) + normalizeAdd"; break;
        case 16: fragmentShaderTemplate[1] = "normalizeMult * vec3(texture(t, UV).r/2, texture(t, UV).g/3, texture(t, UV).b/4) + normalizeAdd"; break; //TODO: This one shows that we might want to normalize across all channels simultaneously...maybe.
        case 17: fragmentShaderTemplate[1] = "normalizeMult * texture(t, UV).rgb * texture(t, UV).rgb + normalizeAdd"; break;
        case 18: fragmentShaderTemplate[1] = "normalizeMult * vec3(mod(texture(t, UV).r, 0.5), mod(texture(t, UV).g, 0.5), mod(texture(t, UV).b, 0.5)) + normalizeAdd"; break;
        case 19: fragmentShaderTemplate[1] = "normalizeMult * vec3(sin(texture(t, UV).r * 6.2831853), sin(texture(t, UV).g* 6.2831853), sin(texture(t, UV).b* 6.2831853)) + normalizeAdd"; break;
    }

    //Allocate memory for a randomized expression
    //The expression will be generated in the pattern of ccOcOcOcOcOcO, where c is a constant or channel and O is an operator. This is effectively a perfectly imbalanced binary tree.
    //If you evaluate it like a stack, the stack will only ever contain two elements at a time.
    unsigned char expression[33]; //33 operators and operands max... may actually be a lot more than needed. That's 16 operators and 17 operands.
    expression[0] = EXP_RANDOM_CHANNEL; //First byte must be an input channel
    int expressionLength = 1;
    for (int x = 1; x < 32; x++) {
        expressionLength++;
        //If x is odd, pick a random channel or constant
        if (x & 1) expression[x] = EXP_RANDOM_CHANNEL_OR_CONSTANT;
        else {
            expression[x] = EXP_RANDOM_OPERATOR; //If x is even, pick a random operator.
            if ((rand() & 1) == 0) break; //There's a 50% chance of not making the expression any longer after each operator.
            /*The probability of having no more than Ops operators in an expression:
                Ops Probability
                1	0.5
                2	0.75
                3	0.875
                4	0.9375
                5	0.96875
                6	0.984375
                7	0.9921875
                8	0.99609375
                9	0.998046875
                10	0.999023438
                11	0.999511719
                12	0.999755859
                13	0.99987793
                14	0.999938965
                15	0.999969482
                16	1.0 */
        }
    }

    //TODO: Store the randomized expression in a struct so we can do stuff like save it to the disk and reload it and show it to the user when they click on the image generated with it.
    fragmentShaderTemplate[1] = expressionToGLSLString(expression, expressionLength);

    RenderToTexture(app, app->usedTextures++);
    free(fragmentShaderTemplate[1]);
    fragmentShaderTemplate[1] = "normalizeMult * vec3(texture(t, UV).rg, 1) + normalizeAdd";
}

static void Animate(APP *app) {
    if (fabs(app->scrollVelocity) > SCROLL_STOP_THRESHOLD) {
        app->updated = TRUE; //Tells whether render is necessary
        //Scroll the screen
        app->scrollMinor += app->scrollVelocity;
        app->scrollVelocity *= 0.97f;
        //Once you've scrolled enough, take away from scrollMinor and add/subtract from/to scrollMajor
        if (app->scrollMinor > SCROLL_PER_ROW && app->scrollMajor < ULONG_MAX - (unsigned long int)rowsPerScreen(app)) {
            app->scrollMajor++;
            app->scrollMinor -= SCROLL_PER_ROW;

            //TODO: Cycle off the oldest expressions/textures and generate a row of new ones (if they haven't already been generated)
            //TODO: This little while loop will generate based on the number of used textures, so it's not for the final product. It's just for testing up to MAX_TEXTURES-RESERVED_TEXTURES images.
            while (app->usedTextures < MAX_TEXTURES && app->usedTextures < IMAGES_PER_ROW * (app->scrollMajor + rowsPerScreen(app) + 1) + RESERVED_TEXTURES) GenerateNewImage(app);
        }
        else if (app->scrollMinor < 0 && app->scrollMajor > 0) {
            app->scrollMajor--;
            app->scrollMinor += SCROLL_PER_ROW;

            //TODO: Cycle off the newest expressions/textures and reload a row of old ones
        }
    }
    //Prevent scrolling above the top (smoothly!) or below the bottom (assuming it were possible to reach the bottom)
    if (app->scrollMajor == 0 && app->scrollMinor < -SCROLL_STOP_THRESHOLD) {
        app->scrollVelocity *= 0.9f;
        app->scrollMinor *= 0.9f;
        app->updated = TRUE; //Tells whether render is necessary
    } else if (app->scrollMajor == ULONG_MAX - (unsigned long int)rowsPerScreen(app) && app->scrollMinor > SCROLL_PER_ROW + SCROLL_STOP_THRESHOLD) {
        app->scrollVelocity *= 0.9f;
        app->scrollMinor = SCROLL_PER_ROW + (app->scrollMinor - SCROLL_PER_ROW) * 0.9f;
        app->updated = TRUE; //Tells whether render is necessary
    }
}



/*****************************************************************************
 *                            Rendering Functions                            *
 *****************************************************************************/


//Draw a scene to OpenGL
static void Render(APP *app) {
	float vector[2];

    glClear(GL_COLOR_BUFFER_BIT);

    //Draw the filtered textures, after rendering the filtered base image to textures
    //TODO: Make this a circular array buffer (along with one for the expressions). Save to disk when generated; unload a row when nearing capacity. Only reload when nearing necessity, though.
    for (int x = RESERVED_TEXTURES; x < app->usedTextures; x++) {
        //Now draw the test texture (render-to-texture)
        vector[0] = 270.0f * ((x - RESERVED_TEXTURES) % IMAGES_PER_ROW);
        vector[1] = app->height + app->scrollMinor - SCROLL_PER_ROW * (1 + (x - RESERVED_TEXTURES) / IMAGES_PER_ROW) + (app->scrollMajor * SCROLL_PER_ROW); //TODO: The last part of this expression is for testing only until I start generating more rows.

        if (vector[1] < -SCROLL_PER_ROW || vector[1] > app->height) continue; //Don't draw off-screen!

        glBindTexture(GL_TEXTURE_2D, app->textures[x]);
        glUniform1f(app->attrib_texture, app->textures[x]);

        glUniform2fv(app->attrib_translation, 1, vector);
        glBindVertexArray(app->VAO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    SDL_GL_SwapWindow(app->window);
}



/*****************************************************************************
 *                             Program Functions                             *
 *****************************************************************************/

//Event handler for mouse down
static void onMouseDown(APP *app, int button, int x, int y) {
	app->buttonDown = button;
	app->oldCursorX = x;
	app->oldCursorY = y;
}

//Event handler for mouse up
static void onMouseUp(APP *app, int button, int x, int y) {
	app->buttonDown = 0;
}

//Event handler for mouse move
static void onMouseMove(APP *app, int x, int y) {
	if (app->buttonDown == SDL_BUTTON_LEFT) {
	} else if (app->buttonDown == SDL_BUTTON_RIGHT) {
	}
	app->oldCursorX = x;
	app->oldCursorY = y;
}

//Event handler for window resize
static void onResize(APP *app, int width, int height) {
    float matrix[16];

    //Configure viewport
    if (width  < 1) width  = 1;
    if (height < 1) height = 1;
    glViewport(0, 0, width, height);

	//Configure orthographic projection matrix
    memset(matrix, 0, sizeof matrix);
    matrix[0] = 2.0f / width;
    matrix[5] = 2.0f / height;
    matrix[10] = 0; //Ignore Z values entirely
	matrix[12] = -1;
	matrix[13] = -1;
    matrix[15] = 1;

    glUniformMatrix4fv(app->attrib_projection, 1, GL_FALSE, matrix);

    //Configure application settings
    app->width  = width;
    app->height = height;
}

static void onMouseWheel(APP *app, int delta) {
    //Scrolling has exponential momentum, so scrolling more when you're already scrolling will make it speed up!
    app->scrollVelocity = app->scrollVelocity * 1.1f - delta * 5.0f;
}

//Process window events
static int DoEvents(APP *app) {
    SDL_Event evt;
    int       quit = 0;

    //Process all available events
    while (SDL_PollEvent(&evt)) switch (evt.type) {

        //Close
        case SDL_QUIT: quit = 1; break;

        //Key down
        case SDL_KEYDOWN:
			//switch (evt.key.keysym.sym) {}
			break;


        //Mouse down
        case SDL_MOUSEBUTTONDOWN:
            onMouseDown(app, evt.button.button, evt.button.x, evt.button.y);
            break;

        //Mouse up
        case SDL_MOUSEBUTTONUP:
            onMouseUp(app, evt.button.button, evt.button.x, evt.button.y);
            break;

        //Mouse motion
        case SDL_MOUSEMOTION:
            onMouseMove(app, evt.motion.x, evt.motion.y);
            break;

        //Mouse scroll
        case SDL_MOUSEWHEEL:
            if (evt.wheel.direction == SDL_MOUSEWHEEL_FLIPPED) onMouseWheel(app, -evt.wheel.y);
            else onMouseWheel(app, evt.wheel.y);
            break;

        case SDL_WINDOWEVENT: switch (evt.window.event) {

        //Resize
        case SDL_WINDOWEVENT_RESIZED:
            onResize(app, evt.window.data1, evt.window.data2);
            break;
        case SDL_WINDOWEVENT_EXPOSED:
            app->updated = TRUE; //Window needs to be redrawn
            break;

        } break;
        default:;
    }

    return quit;
}

//Main program processing loop
static void MainLoop(APP *app) {
    //Timer state
    uint64_t taccum  = 0;
    uint64_t tfreq   = SDL_GetPerformanceFrequency();
    uint64_t tprev   = SDL_GetPerformanceCounter();
    uint64_t ttarget = tfreq / 120;
    uint64_t tthis;

    //Initialize RNG
    srand((unsigned int) tprev);

    //Configure the initial window size
    onResize(app, app->width, app->height);

    //Generate initial images.
    while (app->usedTextures < MAX_TEXTURES && app->usedTextures < IMAGES_PER_ROW * (app->scrollMajor + rowsPerScreen(app) + 1) + RESERVED_TEXTURES) GenerateNewImage(app);

    //Loop until a close event is encountered
    while (!DoEvents(app)) {

        //Calculate timing data
        tthis   = SDL_GetPerformanceCounter();
        taccum += tthis - tprev;
        tprev   = tthis;

        //Process one or more frames
        if (taccum >= ttarget) {

            //Animate for each frame elapsed
            for (; taccum >= ttarget; taccum -= ttarget) {
                Animate(app);
            }

            //Draw only the most recent frame
            if (app->updated) Render(app);
            app->updated = FALSE;
        }

        //Relinquish CPU control to the OS for a moment
        SDL_Delay(1);
    }
}

//Program entry point
int main(int argc, char **argv) {
    APP app;

    memset(&app, 0, sizeof (APP));

    //Initialize application components
    if (InitSDL(&app) || InitGL(&app))
        goto cleanup;

    //Main program processing
    InitApp(&app);
    MainLoop(&app);

//Common cleanup code
cleanup:
    UninitApp(&app);
    UninitGL (&app);
    UninitSDL(&app);
    return 0;
}
