#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <FastLED.h>

// ==============================================
// CONFIGURATION - UPDATE THESE VALUES
// ==============================================
const char* WIFI_SSID = "ssid";           // Replace with your WiFi SSID
const char* WIFI_PASSWORD = "pwd";   // Replace with your WiFi password
const char* WEBSOCKET_HOST = "192.168.15.26";          // Replace with WebSocket server IP/hostname
const int WEBSOCKET_PORT = 8001;                         // Replace with WebSocket server port
const char* WEBSOCKET_PATH = "/";                        // WebSocket path (usually "/" unless specified)

// LED Configuration
#define LED_PIN         2        // GPIO pin connected to LED data line (change if needed)
#define NUM_LEDS        144      // Number of LEDs in the WS2815 RGBW strip
#define LED_TYPE        WS2815
#define COLOR_ORDER     RGB      // Color order for RGB channels - WS2815 commonly uses GRB
#define MAX_BRIGHTNESS  100      // Maximum brightness (0-255) - adjust this to control overall brightness

// ==============================================
// GLOBAL VARIABLES
// ==============================================
CRGB leds[NUM_LEDS];
WebSocketsClient webSocket;

// Startup animation variables
bool startupMode = true;              // Whether we're in startup animation mode

// Flash effect variables
float flashDuration = 0.0;        // Total flash duration in seconds
float flashStartTime = 0.0;       // When the flash started (millis)
bool flashActive = false;         // Whether flash effect is currently active

// Health bar variables
int playerHealth = 100;           // Current player health (0-100)
bool healthChanged = true;        // Flag to update health display

// Death animation variables
bool deathAnimationActive = false; // Whether death animation is running
float deathStartTime = 0.0;       // When death animation started
float deathBlinkDuration = 1.5;   // Duration for 3 slow blinks (1.5 seconds)
float deathFadeDuration = 2.0;    // Duration for edge-to-center fade (2 seconds)
float deathTotalDuration = 3.5;   // Total animation duration

// ==============================================
// LED CONTROL FUNCTIONS
// ==============================================
// ==============================================
// STARTUP ANIMATION FUNCTIONS
// ==============================================
void updateStartupAnimation() {
  if (!startupMode) return;
  
  // Use millis() directly with modulo to avoid overflow issues
  // Complete rainbow cycle every 5000ms (5 seconds)
  unsigned long cycleTime = millis() % 5000;  // 0 to 4999ms cycle
  
  // Convert to hue value (0-255)
  uint8_t baseHue = (uint8_t)((cycleTime * 255UL) / 5000UL);
  
  // Fill the entire strip with a smooth rainbow that moves over time
  for (int i = 0; i < NUM_LEDS; i++) {
    // Create a rainbow pattern across the strip
    uint8_t pixelHue = baseHue + (i * 255 / NUM_LEDS);
    CHSV hsvColor(pixelHue, 255, 255);  // Full saturation and brightness
    leds[i] = hsvColor;
  }
  
  FastLED.show();
}

// ==============================================
// DEATH ANIMATION FUNCTIONS  
// ==============================================
void startDeathAnimation() {
  Serial.println("[Death] Player died! Starting new death animation");
  deathAnimationActive = true;
  deathStartTime = millis() / 1000.0;
  
  // Death animation overrides everything (flash and health)
  flashActive = false;
}

void updateDeathAnimation() {
  if (!deathAnimationActive) return;
  
  float currentTime = millis() / 1000.0;
  float elapsedTime = currentTime - deathStartTime;
  
  if (elapsedTime >= deathTotalDuration) {
    // Death animation finished - return to health display
    deathAnimationActive = false;
    healthChanged = true;
    Serial.println("[Death] Death animation completed - returning to health display");
    return;
  }
  
  if (elapsedTime < deathBlinkDuration) {
    // Phase 1: 3 slow blinks (0.5 seconds each)
    float blinkCycle = fmod(elapsedTime, 0.5);  // 0.5 second cycle for each blink
    bool isOn = blinkCycle < 0.25;  // On for 0.25s, off for 0.25s
    
    if (isOn) {
      // All LEDs bright red
      fill_solid(leds, NUM_LEDS, CRGB(255, 0, 0));
    } else {
      // All LEDs off
      fill_solid(leds, NUM_LEDS, CRGB::Black);
    }
    
  } else {
    // Phase 2: Edge-to-center fade out effect with exponential acceleration
    float fadeTime = elapsedTime - deathBlinkDuration;  // Time since blink phase ended
    float linearProgress = fadeTime / deathFadeDuration;  // 0.0 to 1.0
    
    // Apply exponential curve: starts slow, accelerates dramatically toward the end
    // Using power of 0.4 gives nice acceleration curve (starts gentle, ends fast)
    float exponentialProgress = pow(linearProgress, 0.4);
    
    // Calculate how many LEDs should be off from each edge
    int ledsOff = (int)(exponentialProgress * (NUM_LEDS / 2));
    
    // Fill strip with red first
    fill_solid(leds, NUM_LEDS, CRGB(255, 0, 0));
    
    // Turn off LEDs from both edges moving toward center
    for (int i = 0; i < ledsOff && i < NUM_LEDS / 2; i++) {
      leds[i] = CRGB::Black;                    // Turn off from start
      leds[NUM_LEDS - 1 - i] = CRGB::Black;    // Turn off from end
    }
  }
  
  FastLED.show();
}

// ==============================================
// FLASH EFFECT FUNCTIONS
// ==============================================
void startFlashEffect(float duration) {
  Serial.printf("[Flash] Starting flash effect: %.3f seconds\n", duration);
  flashDuration = duration;
  flashStartTime = millis() / 1000.0;  // Convert to seconds
  flashActive = true;
}

void updateFlashEffect() {
  if (!flashActive) return;
  
  float currentTime = millis() / 1000.0;  // Current time in seconds
  float elapsedTime = currentTime - flashStartTime;
  float remainingTime = flashDuration - elapsedTime;
  
  if (remainingTime <= 0) {
    // Flash effect finished - health display is already showing
    flashActive = false;
    Serial.println("[Flash] Flash effect completed - health bar already visible");
    return;
  }
  
  // Calculate flash intensity using power function - looks better!
  float normalizedTime = remainingTime / flashDuration;  // 1.0 at start, 0.0 at end
  float flashIntensity = pow(normalizedTime, 2.5);  // Smooth fade that just works
  
  // Calculate health bar colors for each LED
  int healthLeds = (playerHealth * NUM_LEDS) / 100;
  
  // Blend flash white with health colors
  for (int i = 0; i < NUM_LEDS; i++) {
    CRGB healthColor = CRGB::Black;  // Default to off
    
    // Calculate health color for this LED position
    if (i < healthLeds) {
      float healthPercent = (float)playerHealth / 100.0;
      
      if (healthPercent > 0.6) {
        // Green zone (60-100% health)
        float greenIntensity = (healthPercent - 0.6) / 0.4;
        healthColor = CRGB(255 * (1.0 - greenIntensity), 255, 0);  // Yellow to Green
      } else if (healthPercent > 0.3) {
        // Yellow zone (30-60% health)  
        float yellowIntensity = (healthPercent - 0.3) / 0.3;
        healthColor = CRGB(255, 255 * yellowIntensity, 0);  // Red to Yellow
      } else {
        // Red zone (0-30% health)
        healthColor = CRGB(255, 0, 0);  // Pure red
      }
    }
    
    // Blend white flash with health color
    uint8_t flashWhite = (uint8_t)(flashIntensity * 255);
    CRGB flashColor = CRGB(flashWhite, flashWhite, flashWhite);
    
    // Linear blend: more flash at start, more health color as flash fades
    leds[i].r = (uint8_t)(flashColor.r * flashIntensity + healthColor.r * (1.0 - flashIntensity));
    leds[i].g = (uint8_t)(flashColor.g * flashIntensity + healthColor.g * (1.0 - flashIntensity));
    leds[i].b = (uint8_t)(flashColor.b * flashIntensity + healthColor.b * (1.0 - flashIntensity));
  }
  
  FastLED.show();
}

void displayHealthBar() {
  // Calculate how many LEDs to light based on health (0-100 → 0-144 LEDs)
  int healthLeds = (playerHealth * NUM_LEDS) / 100;
  
  // Clear all LEDs first
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  
  // Fill health bar with gradient colors
  for (int i = 0; i < healthLeds; i++) {
    float healthPercent = (float)playerHealth / 100.0;
    
    CRGB color;
    if (healthPercent > 0.6) {
      // Green zone (60-100% health)
      float greenIntensity = (healthPercent - 0.6) / 0.4;  // 0.0 to 1.0
      color = CRGB(255 * (1.0 - greenIntensity), 255, 0);  // Yellow to Green
    } else if (healthPercent > 0.3) {
      // Yellow zone (30-60% health)  
      float yellowIntensity = (healthPercent - 0.3) / 0.3;  // 0.0 to 1.0
      color = CRGB(255, 255 * yellowIntensity, 0);  // Red to Yellow
    } else {
      // Red zone (0-30% health)
      color = CRGB(255, 0, 0);  // Pure red
    }
    
    leds[i] = color;
  }
  
  FastLED.show();
}

void processMessage(String message) {
  // Exit startup mode on first message
  if (startupMode) {
    startupMode = false;
    Serial.println("[Startup] First WebSocket message received - exiting startup mode");
    healthChanged = true;  // Force health bar update
  }
  
  // Parse "flash:4.48975" format
  if (message.startsWith("flash:")) {
    String durationStr = message.substring(6);  // Remove "flash:" prefix
    float newDuration = durationStr.toFloat();
    
    if (newDuration > 0) {
      if (!flashActive) {
        // No flash currently active, start new one
        startFlashEffect(newDuration);
      } else {
        // Flash already active, check if we should extend/restart
        float currentTime = millis() / 1000.0;
        float elapsedTime = currentTime - flashStartTime;
        float remainingTime = flashDuration - elapsedTime;
        
        if (newDuration > remainingTime) {
          Serial.printf("[Flash] Extending flash: %.3f > %.3f remaining\n", newDuration, remainingTime);
          startFlashEffect(newDuration);  // Restart with new duration
        } else {
          Serial.printf("[Flash] Ignoring flash: %.3f <= %.3f remaining\n", newDuration, remainingTime);
        }
      }
    }
  }
  // Parse "localPlayer:93" format  
  else if (message.startsWith("localPlayer:")) {
    String healthStr = message.substring(12);  // Remove "localPlayer:" prefix
    int newHealth = healthStr.toInt();
    
    if (newHealth >= 0 && newHealth <= 100) {
      if (newHealth != playerHealth) {
        int previousHealth = playerHealth;
        playerHealth = newHealth;
        healthChanged = true;
        Serial.printf("[Health] Player health updated: %d\n", playerHealth);
        
        // Check for death (health went to 0)
        // Only trigger if not already in death animation to avoid restarts
        if (newHealth == 0 && previousHealth > 0 && !deathAnimationActive) {
          startDeathAnimation();
        }
      }
    }
  }
}

// ==============================================
// WEBSOCKET EVENT HANDLER
// ==============================================
void webSocketEvent(WStype_t type, uint8_t * payload, size_t length) {
  switch(type) {
    case WStype_DISCONNECTED:
      Serial.println("[WebSocket] Disconnected");
      ESP.restart();
      break;
      
    case WStype_CONNECTED:
      Serial.printf("[WebSocket] Connected to: %s\n", payload);
      break;
      
    case WStype_TEXT:
      Serial.printf("[WebSocket] Received text: %s\n", payload);
      // Process flash and health messages
      processMessage(String((char*)payload));
      break;

    case WStype_ERROR:
      Serial.printf("[WebSocket] Error occurred: %s\n", payload);
      Serial.println("[WebSocket] Check server status and network connection");
      ESP.restart();
      break;
      
    default:
      Serial.printf("[WebSocket] Unknown event type: %d\n", type);
      break;
  }
}

// ==============================================
// WIFI CONNECTION FUNCTION
// ==============================================
void connectToWiFi() {
  Serial.print("[WiFi] Connecting to ");
  Serial.println(WIFI_SSID);
  
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(1000);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println();
    Serial.println("[WiFi] Connected!");
    Serial.print("[WiFi] IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println();
    Serial.println("[WiFi] Failed to connect!");
    Serial.println("[WiFi] Please check your credentials and try again.");
  }
}

// ==============================================
// SETUP FUNCTION
// ==============================================
void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  Serial.println("\n==============================================");
  Serial.println("WS2815 WebSocket Client LED Controller");
  Serial.println("==============================================");
  
  // Initialize FastLED
  FastLED.addLeds<LED_TYPE, LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(MAX_BRIGHTNESS);  // Full brightness
  Serial.println("[LED] FastLED initialized");
  
  // Initialize startup animation
  Serial.println("[Startup] Starting rainbow cycling animation - waiting for first WebSocket message");
  
  // Connect to WiFi
  connectToWiFi();
  
  // Only proceed if WiFi is connected
  if (WiFi.status() == WL_CONNECTED) {
    // Initialize WebSocket client
    webSocket.begin(WEBSOCKET_HOST, WEBSOCKET_PORT, WEBSOCKET_PATH);
    webSocket.onEvent(webSocketEvent);
    webSocket.setReconnectInterval(5000);  // Reconnect every 5 seconds if disconnected
    
    // Disable heartbeat - your server might not handle ping/pong properly
    // webSocket.enableHeartbeat(15000, 3000, 2);
    
    Serial.printf("[WebSocket] Connecting to ws://%s:%d%s\n", 
                  WEBSOCKET_HOST, WEBSOCKET_PORT, WEBSOCKET_PATH);
    Serial.println("[WebSocket] Heartbeat disabled for compatibility");
  } else {
    Serial.println("[ERROR] Cannot initialize WebSocket without WiFi connection");
  }
  
  Serial.println("==============================================");
  Serial.println("Setup complete. Ready for CS2 integration!");
  Serial.println("Rainbow startup animation active until first WebSocket message");
  Serial.println("Commands:");
  Serial.println("  'flash:X.XXX' - Flashbang effect (X.XXX = seconds)");
  Serial.println("  'localPlayer:XX' - Health bar (XX = 0-100)");
  Serial.println("  'localPlayer:0' - Triggers death animation (3 slow blinks + edge fade)");
  Serial.println("==============================================");
}

// ==============================================
// MAIN LOOP
// ==============================================
void loop() {
  // Handle WebSocket events (only if WiFi is connected)
  if (WiFi.status() == WL_CONNECTED) {
    webSocket.loop();
  } else {
    // If WiFi disconnected, try to reconnect
    Serial.println("[WiFi] Connection lost. Attempting to reconnect...");
    connectToWiFi();
    delay(5000);
  }

  // Update effects in priority order: Startup > Death > Flash > Health
  
  // 1. Startup animation has highest priority (until first message)
  if (startupMode) {
    updateStartupAnimation();
    delay(10);
    return;  // Skip all other animations during startup
  }
  
  // 2. Death animation has highest priority (after startup)
  updateDeathAnimation();
  
  // 3. Flash effect (only if no death animation)
  if (!deathAnimationActive) {
    updateFlashEffect();
  }
  
  // 4. Health bar (only if no death or flash active)
  if (!deathAnimationActive && !flashActive && healthChanged) {
    displayHealthBar();
    healthChanged = false;  // Reset flag after updating display
  }
  
  // Small delay to prevent overwhelming the processor
  delay(10);
}
