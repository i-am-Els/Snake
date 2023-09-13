#define GLEW_STATIC
#include "GLEW/glew.h"
#include <iostream>
#include <vector>
#include <cmath>
#include <ctime>
#include <random>
#include <SDL2/SDL.h>
#include "SDL2/SDL_audio.h"
// #include "SDL_ttf.h"

// **********************************************************************************************
//	Global Variable Declarations
// **********************************************************************************************
const int WIDTH = 600, HEIGHT = 600;
const int SUCCESS = 0, FAILED = -1;

SDL_Window* myWindow = nullptr;
SDL_GLContext myContext = nullptr;

enum eDirection {STOP = 0, LEFT, RIGHT, UP, DOWN};
enum eDifficulty {EASY = 0, MEDIUM, HARD [[maybe_unused]]
};

// **********************************************************************************************
//	Shader Setups
// **********************************************************************************************
const char* vertexSource = 
	"#version 330 core\n"
	"layout(location = 0) in vec2 position;\n"
	"void main(){\n"
	"	gl_Position = vec4(position.xy, 0.0f, 1.0f);\n"
	"}\n";

const char* fragmentSource =
	"#version 330 core\n"
	"out vec4 color;\n"
	"void main(){\n"
	"	color = vec4(0.1f, 0.5f, 0.1f, 1.0f);\n"
	"}\n";

static unsigned int compileShader(unsigned int type, const std::string& source)
{
 	unsigned int id = glCreateShader(type);
	const char* src = source.c_str();

	glShaderSource(id, 1, &src, nullptr);
	glCompileShader(id);

	int result;
	glGetShaderiv(id, GL_COMPILE_STATUS, &result);
	if (result == GL_FALSE){
		int length;
		glGetShaderiv(id, GL_INFO_LOG_LENGTH, &length);
		char* message = (char*)malloc(length*sizeof(char));
		glGetShaderInfoLog(id, length, &length, message);
		std::cout << "Failed to Compile " << (type == GL_VERTEX_SHADER ? "Vertex" : "Fragment") << " shader!" << std::endl;
		std::cout << message << std::endl; 
		glDeleteShader(id);
		free(message);
		return 0;
	}

	return id;
}

static unsigned int SetUpShaders(const std::string& vertex, const std::string& fragment)
{
	unsigned int program = glCreateProgram();
	unsigned int vs = compileShader(GL_VERTEX_SHADER, vertex);
	unsigned int fs = compileShader(GL_FRAGMENT_SHADER, fragment);

	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	glValidateProgram(program);

	glDeleteShader(vs);
	glDeleteShader(fs);

	return program;
}

// **********************************************************************************************
//	Structs and namespace Declarations & Definitions
// **********************************************************************************************

struct Vector
{
    Vector() 
	{
		x = 0.0f;
		y = 0.0f;
	}
    
	Vector(float _x, float _y) 
	{
		x = _x;
		y = _y;
	}
    
	Vector(const Vector& v) 
	{
		x = v.x;
		y = v.y;
	}
    
	~Vector()= default;

	static float Distance(const Vector& _this, const Vector& other)
	{
		float x = (other.x - _this.x);
		float y = (other.y - _this.y);
		float d = std::sqrt((x*x) + (y*y));
		return d;
	} 

    Vector operator+(const Vector& other) const 
	{
        return {x + other.x, y + other.y};
    }

    Vector operator-(const Vector& other) const 
	{
        return {x - other.x, y - other.y};
    }

    Vector operator*(float s) const 
	{
        return {x * s, y * s};
    }

    Vector operator/(float s) const 
	{
        if (s != 0) {
            return {x / s, y / s};
        } else {
            return *this;
        }
    }
    
	Vector& operator+=(const Vector& other) 
	{
        x += other.x;
        y += other.y;
        return *this;
    }

    Vector& operator-=(const Vector& other) 
	{
        x -= other.x;
        y -= other.y;
        return *this;
    }

    Vector& operator*=(float s) 
	{
        x *= s;
        y *= s;
        return *this;
    }

    Vector& operator/=(float s) 
	{
        if (s != 0) {
            x /= s;
            y /= s;
        }
        return *this;
    }

    float x, y;
};

Vector operator*(float s, const Vector& v) 
{
    return {v.x * s, v.y * s};
}

struct AudioSource{
    Uint8* data;        // Pointer to audio data
    Uint32 length;      // Length of the audio data in bytes
//    Uint32 position;    // Current position in the audio data
    SDL_AudioSpec spec; // Audio specification for this source
};

namespace Global
{
	unsigned int shader = 0, VBO = 0, VAO = 0;
	std::vector<AudioSource> audioSources;
//	SDL_AudioSpec audioSpec;
	SDL_AudioDeviceID audioDevice;
	bool appIsRunning = true;
	bool gameIsPaused = false;
	bool gameOver = false;
	Vector tailOffset = Vector();
	float tailSpacing = 0.070f;
	unsigned int previousTime = SDL_GetTicks();
	unsigned int currentTime = 0;
	float deltaTime = 0.0f;
	unsigned int level = 1;
	eDirection dir = eDirection::UP;
	eDifficulty difficulty = eDifficulty::EASY;
	float step = 0.0f;
	float dX=0.0f, dY=0.0f;
	unsigned int score = 0;
	unsigned int highScore = 0;
	bool tabPressed = false;
	unsigned int maxLevelScore = 5;
	unsigned int fruitSpawnTime = 0;
	unsigned int fruitLifeSpan = 15000;
	bool startGame = false;
};

struct Transform
{
	Transform()
	{
		position = Vector();
	}
	
	explicit Transform(const Vector& _position) : position (_position){}
	Vector position;

	[[nodiscard]] float* GenQuadVertices(float rad) const
	{
		return new float[8]{
			position.x - rad, position.y + rad,
			position.x + rad, position.y + rad,
			position.x - rad, position.y - rad,
			position.x + rad, position.y - rad
		};
	}

	void Translate(float*& verts, float dX, float dY)
	{
		position.x += dX;
		position.y += dY;
		for (int i=0; i < 8; i++){
			if(i%2 == 0)
				verts[i] += dX;
			else
				verts[i] += dY;
		}
	}

	static void Rotate(float* verts, float angle)
	{
		float arr[8] = {
			verts[0] * std::cos(angle) + verts[1] * -1 * std::sin(angle),
			verts[0] * std::sin(angle) + verts[1] * std::cos(angle),
			verts[2] * std::cos(angle) + verts[3] * -1 * std::sin(angle),
			verts[2] * std::sin(angle) + verts[3] * std::cos(angle),
			verts[4] * std::cos(angle) + verts[5] * -1 * std::sin(angle),
			verts[4] * std::sin(angle) + verts[5] * std::cos(angle),
			verts[6] * std::cos(angle) + verts[7] * -1 * std::sin(angle),
			verts[6] * std::sin(angle) + verts[7] * std::cos(angle)
		};
		verts[0] = arr[0];
		verts[1] = arr[1];
		verts[2] = arr[2];
		verts[3] = arr[3];
		verts[4] = arr[4];
		verts[5] = arr[5];
		verts[6] = arr[6];
		verts[7] = arr[7];
	}

	static void Scale(float* verts, float sX, float sY)
	{
		for (int i=0; i < 8; i++){
			if(i%2 == 0)
				verts[i] *= sX;
			else
				verts[i] *= sY;
		}
	}
};

struct Entity
{
    Entity() 
	{
		transform = Transform();
		setVertices();
	}

    explicit Entity(const Vector& _position, float scaleF = 0.025f)
	{
		transform = Transform(_position);
        scaleFactor = scaleF;
		setVertices(scaleFactor);
	}
    
	Entity(const Entity& e, float scaleF = 0.025f)
	{
		transform = e.transform;
        scaleFactor = e.scaleFactor;
		setVertices(scaleFactor);
	}
	
	Entity& operator=(const Entity& other) 
	{
        if (this == &other) {
            return *this; // Self-assignment, no need to do anything
        }

        transform = other.transform;
        scaleFactor = other.scaleFactor;
        setVertices(scaleFactor);
        return *this;
    }

    ~Entity()= default;
    Transform transform;
	float* vertices{};
	Vector oldPosition;
	float scaleFactor;

	void setVertices(float scaleF = 0.025f)
	{
		vertices = transform.GenQuadVertices(scaleF);
        scaleFactor = scaleF;
	}

	void printEntity() const
	{
		std::cout << transform.position.x << ", " << transform.position.y << std::endl;
		for (int i=0; i < 8;){
			std::cout << vertices[i++] << ", "<< vertices[i++] << std::endl;
		}
		std::cout << "\n";
	}

	void SetPosition(float x, float y, float scale = 0.25f)
	{
		oldPosition = transform.position;
		transform.position.x = x;
		transform.position.y = y;
		setVertices(scale);
	}

	void SetPosition(const Vector& v, float scaleF = 0.25f)
	{
		oldPosition = transform.position;
		transform.position = v;
		setVertices(scaleF);
	}
	
	void SetOldPosition(const Vector& pos)
	{
		oldPosition = pos;
	}
};

// **********************************************************************************************
//	Visual and Audio
// **********************************************************************************************

void RenderEntity(Entity& entity)
{
	glBindBuffer(GL_ARRAY_BUFFER, Global::VBO);
	glBufferData(GL_ARRAY_BUFFER, 4 * 2 * sizeof(float), entity.vertices, GL_DYNAMIC_DRAW);
		
	// render here
	glUseProgram(Global::shader);
	glBindVertexArray(Global::VAO);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glBindVertexArray(0);
}

void RenderText()
{
	std::string scoreText = "Score: " + std::to_string(Global::score);
	std::string highScoreText = "High Score: " + std::to_string(Global::highScore);
}

void LoadAudio(const char* filename)
{
    SDL_AudioSpec spec;
    Uint8* data;
    Uint32 length;
    if (SDL_LoadWAV(filename, &spec, &data, &length) != nullptr)
    {
        AudioSource sound;
        sound.data = data;
        sound.length = length;
        sound.spec = spec;
        Global::audioSources.push_back(sound);
    }else
		std::cout << "Failed to load Audio: " << SDL_GetError() << std::endl;
}

void PlayCollisionSound()
{
    if (!Global::audioSources.empty())
    {
        SDL_AudioSpec spec = Global::audioSources[0].spec;
        SDL_QueueAudio(Global::audioDevice, Global::audioSources[0].data, Global::audioSources[0].length);
        SDL_PauseAudioDevice(Global::audioDevice, 0);
    }
}

void PlayGameOverSound()
{
    if (!Global::audioSources.empty() && Global::audioSources.size() > 1)
    {
        SDL_AudioSpec spec = Global::audioSources[1].spec;
        SDL_QueueAudio(Global::audioDevice, Global::audioSources[1].data, Global::audioSources[1].length);
        SDL_PauseAudioDevice(Global::audioDevice, 0);
    }
}

void SetUpAudio()
{
		// Set audio specifications
	SDL_AudioSpec want, have;
	SDL_memset(&want, 0, sizeof(want));
	want.freq = 44100;  // Sample rate
	want.format = AUDIO_S16SYS;  // Sample format
	want.channels = 2;  // Number of audio channels
	want.samples = 4096;  // Audio buffer size

	Global::audioDevice = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);

	if (Global::audioDevice == 0)
	{
		std::cerr << "Failed to open audio: " << SDL_GetError() << std::endl;
		return;
	}

	// Load audio files
	LoadAudio("../assets/audio/carrotnom-92106.wav");
	LoadAudio("../assets/audio/mixkit-retro-game-over-1947.wav");
}

void CleanUpAudio()
{
	SDL_CloseAudioDevice(Global::audioDevice);
}

// **********************************************************************************************
//	Application Window Setup, Render and Cleanup functions
// **********************************************************************************************

inline int SetUpApp(SDL_Window*& window, SDL_GLContext& context)
{
	if (SDL_Init(SDL_INIT_EVERYTHING) != 0)
	{
		std::cout << "Error Initializing SDL: " << SDL_GetError() << std::endl;
		return FAILED;
	}

	// if (TTF_Init() == -1) {
	// 	std::cerr << "TTF_Init failed: " << TTF_GetError() << std::endl;
	// }

	// Setup Sound  here
	SetUpAudio();

	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, 32);

	// Enable Double buffering
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	// OpenGl Core profile
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	// Set OpenGL Version
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);

	window = SDL_CreateWindow("Snake Game", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, WIDTH, HEIGHT, SDL_WINDOW_OPENGL);

	if (!window)
	{
		std::cout << "Error creating a window: " << SDL_GetError() << std::endl;
		return FAILED;
	}

	context = SDL_GL_CreateContext(window);
	if(context == nullptr)
	{
		std::cout << "Error creating an OPENGL Context: " << SDL_GetError() << std::endl;
		return FAILED;
	}
	if (glewInit() != GLEW_OK)
	{
		std::cout << "Error Initializing GLEW" << std::endl;
		return FAILED;
	}
	
	// Explicit VAO creation
	glGenVertexArrays(1, &Global::VAO);
	glBindVertexArray(Global::VAO);

	// VBO
	glGenBuffers(1, &Global::VBO);
	glBindBuffer(GL_ARRAY_BUFFER, Global::VBO);
	glBufferData(GL_ARRAY_BUFFER, 4 * 2 * sizeof(float), nullptr, GL_DYNAMIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float) * 2, (void*)0);

	glBindBuffer(GL_ARRAY_BUFFER, 0);

	Global::shader = SetUpShaders(vertexSource, fragmentSource);
	return SUCCESS;
}

inline void CleanUpApp(SDL_Window*& window, SDL_GLContext& context)
{
	SDL_GL_DeleteContext(context);
	SDL_DestroyWindow(window);
	// CleanUp Audio Device here
	CleanUpAudio();
	SDL_Quit();
}

// **********************************************************************************************
//	Game and Utility Functions
// **********************************************************************************************

bool HasCollided(Entity& _this, Entity& other)
{
	float distance = Vector::Distance(_this.transform.position, other.transform.position);
	if (distance < other.scaleFactor * 2)
		return true;
	return false; 
}

Vector GenerateRandomPoint()
{
	std::mt19937 gen(static_cast<unsigned int>(std::time(nullptr)));

    std::uniform_real_distribution<float> distribution(-0.975f, 0.976f);

    float x = distribution(gen);
    float y = distribution(gen);

    return {x, y};
}

void GameOver()
{
	Global::dir = eDirection::STOP;
	Global::gameOver = true;
	if (Global::score > Global::highScore)
	{
		Global::highScore = Global::score;
	}
	Global::level = 1;
	Global::maxLevelScore = 5;
	// Play Game Over Sound here;
	if (Global::startGame)
		PlayGameOverSound();
}

void ResetGame()
{
	Global::gameOver = false;
	Global::gameIsPaused = false;
	Global::score = 0;
	if (Global::dir != eDirection::DOWN) 
	{
		Global::dX = 0.0f; Global::dY = Global::step;
		Global::tailOffset = Vector(0.0f, -0.07f);
		Global::dir = eDirection::UP;
	}
}

void SetDifficulty(eDifficulty d)
{
	Global::difficulty = d;
	if (Global::difficulty == eDifficulty::EASY)
	{
		Global::step = 0.25f;
		Global::fruitLifeSpan = 15000;
	}
	else if (Global::difficulty == eDifficulty::MEDIUM)
	{
		Global::step = 0.45f;
		Global::fruitLifeSpan = 10000;
	}
	else
	{
		Global::step = 0.65f;
		Global::fruitLifeSpan = 5000;
	}
}

void HandleInput(SDL_Event& event, Entity& snake, std::vector<Entity>& tails)
{
	while (SDL_PollEvent(&event)) 
	{
        if (event.type == SDL_QUIT){
			// Event to close the application
            Global::appIsRunning = false;
        }else if(event.key.keysym.sym == SDLK_ESCAPE)
		{
			// Escape Key to Quit the application
			Global::appIsRunning = false;
		}else if(event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_RETURN && Global::gameOver)
		{
			Global::dX = 0.0f; Global::dY = 0.0f; 
			snake.SetPosition(Vector(), 0.035f);
			tails.clear();
			Global::dir = eDirection::STOP;
			// Reset Fruit LifeSpan
			Global::fruitSpawnTime = Global::currentTime;
			ResetGame();
		}else if(event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_TAB && Global::gameOver && !Global::tabPressed)
		{
			int type = ((Global::difficulty + 1) % 3);
			Global::difficulty = (eDifficulty)type;
			SetDifficulty(Global::difficulty);
			Global::tabPressed = true;
		}else if (event.type == SDL_KEYUP && event.key.keysym.sym == SDLK_TAB && Global::gameOver)
		{
			Global::tabPressed = false;
		}

		if (event.type == SDL_KEYDOWN )
		{
			if (event.key.keysym.sym == SDLK_LEFT || event.key.keysym.sym == SDLK_a)
			{
				Global::gameIsPaused = false;
				if (Global::dir != eDirection::RIGHT) 
				{
					Global::dX = -Global::step; Global::dY = 0.0f;
					Global::tailOffset = Vector(0.07f, 0.0f);
					Global::dir = eDirection::LEFT;
				}
				break;
			}
			else if (event.key.keysym.sym == SDLK_RIGHT || event.key.keysym.sym == SDLK_d)
			{
				Global::gameIsPaused = false;
				if (Global::dir != eDirection::LEFT) 
				{
					Global::dX = Global::step; Global::dY = 0.0f;
					Global::tailOffset = Vector(-0.07f, 0.0f);
					Global::dir = eDirection::RIGHT;
				}
				break;
			}
			else if (event.key.keysym.sym == SDLK_UP || event.key.keysym.sym == SDLK_w)
			{
				Global::gameIsPaused = false;
				if (Global::dir != eDirection::DOWN) 
				{
					Global::dX = 0.0f; Global::dY = Global::step;
					Global::tailOffset = Vector(0.0f, -0.07f);
					Global::dir = eDirection::UP;
				}
				break;
			}
			else if (event.key.keysym.sym == SDLK_DOWN || event.key.keysym.sym == SDLK_s)
			{
				Global::gameIsPaused = false;
				if (Global::dir != eDirection::UP) 
				{
					Global::dX = 0.0f; Global::dY = -Global::step;
					Global::tailOffset = Vector(0.0f, 0.07f);
					Global::dir = eDirection::DOWN;
				}
				break;
			}
			else if (event.key.keysym.sym == SDLK_SPACE) 
			{
			 	Global::dX = 0.0f; Global::dY = 0.0f; 
			 	Global::gameIsPaused = true; 
			 	break;
			}
		}
    }
}

void NewLevel(Entity& snake, std::vector<Entity>& tails)
{
	Global::level++;
	tails.clear();
	snake.SetPosition(Vector(), 0.035f);
	Global::dir = eDirection::STOP;
	Global::maxLevelScore += (Global::level * 5);
}

void UpdateGame(Entity& snake, Entity& fruit, Entity& tail, std::vector<Entity>& tails)
{
	if (!Global::gameOver)
	{
		if (!Global::gameIsPaused)
		{
			// Calculating Fruit LifeSpan
			// Fruit must disappear after 10 seconds
			if (Global::currentTime - Global::fruitSpawnTime > Global::fruitLifeSpan)
			{
				fruit.SetPosition(GenerateRandomPoint(), 0.025f);
				Global::fruitSpawnTime = Global::currentTime;
			}

			// Update Function;
			snake.SetOldPosition(snake.transform.position);
			snake.transform.Translate(snake.vertices, Global::dX * Global::deltaTime, Global::dY * Global::deltaTime);

			for (int i = tails.size() - 1; i >= 0; i--)
			{
				if (i == 0) 
				{
					Vector direction = snake.transform.position - tails[i].transform.position;
					direction = direction / Vector::Distance(snake.transform.position, tails[i].transform.position);
					Vector offset = direction * Global::tailSpacing;
					tails[i].SetPosition(snake.transform.position - offset, 0.030f);
				} 
				else 
				{
					Vector direction = tails[i - 1].transform.position - tails[i].transform.position;
					direction = direction / Vector::Distance(tails[i - 1].transform.position, tails[i].transform.position);
					Vector offset = direction * Global::tailSpacing;
					tails[i].SetPosition(tails[i - 1].transform.position - offset, 0.030f);

					// Check collision of snake and its tail
					if (HasCollided(snake, tails[i])) 
					{
						GameOver();
						break;
					}
				}
			}

			// check collision of snake and fruit
			if (HasCollided(snake, fruit))
			{
				fruit.SetPosition(GenerateRandomPoint(), 0.025f);
				if (tails.empty())
					tail = Entity(snake.oldPosition + Global::tailOffset, 0.030f);
				else
					tail = Entity(tails[tails.size() - 1].oldPosition + Global::tailOffset, 0.030f);
				tails.push_back(tail);
				Global::score++;

				// Reset Fruit LifeSpan
				Global::fruitSpawnTime = Global::currentTime;
				
				// Check if the game should move to the next level 
				if (Global::score == Global::maxLevelScore)
					NewLevel(snake, tails);

				// Play Collision Sound here
				PlayCollisionSound();
			}
			
			// check collision of snake and wall
			else if (snake.transform.position.x < -0.999f || snake.transform.position.x > 0.999f || snake.transform.position.y < -0.999f || snake.transform.position.y > 0.999f)
				GameOver();
		}
		// else{
		// 	// resume game screen pops up
		// }
	}
	// else{

	// }
}

void RenderGame(Entity& snake, Entity& fruit, std::vector<Entity>& tails)
{
	RenderEntity(snake);
	RenderEntity(fruit);
	for (Entity& tail : tails)
	{
		RenderEntity(tail);
	}
}

// **********************************************************************************************
//	Application Entry Point
// **********************************************************************************************

int WinMain(int argc, char* argv[])
{
	if (SetUpApp(myWindow, myContext) == -1)
	{
		return FAILED;
	}

	Entity snake(Vector(0.0f, 0.0f), 0.035f);
	std::vector<Entity> tails;
	Entity fruit(GenerateRandomPoint(), 0.025f);
	Entity tail;

	// Set the default difficulty to Easy 
	SetDifficulty(eDifficulty::EASY);
	
	// Pause Update Game Here at the Beginning to enable Changing Difficulty with the TAB key
	GameOver();
	Global::startGame = true;

	// Main Game Loop
	while(Global::appIsRunning)
	{
		glViewport(0, 0, WIDTH, HEIGHT); 
		// Calculating deltaTime
		Global::currentTime = SDL_GetTicks();
		Global::deltaTime = (float)(Global::currentTime - Global::previousTime) / 1000.0f;
		Global::previousTime = Global::currentTime;

		// Handle Input
		SDL_Event event;
        HandleInput(event, snake, tails);


		glClear(GL_COLOR_BUFFER_BIT);
		glClearColor(0.1f, 0.8f, 0.3f, 1.0f);

		// Update Game state 
		UpdateGame(snake, fruit, tail, tails);
		
		// Update Render Buffer and Render 
		RenderGame(snake, fruit, tails);
		
		// Swap Buffer
		SDL_GL_SwapWindow(myWindow);
	}

	CleanUpApp(myWindow, myContext);
	return SUCCESS;
}

