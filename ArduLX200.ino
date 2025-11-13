/*
 * Arduino LX200 Emulator
 * - Great-circle slew (Slerp)
 * - Constant speed 2°/s
 * - 10-second delay before movement starts
 * - Sidereal tracking
 * - LED status
 */

struct Coord {
  float ra;   // hours
  float dec;  // degrees
};

Coord current = {2.5303, 89.2642}; // Polaris
Coord target  = current;

String inputBuffer = "";

// Slew control
bool slewing = false;          // true when actively moving
bool slewPending = false;      // true when waiting delay
unsigned long slewCommandTime = 0;
const unsigned long slewDelay = 10000; // 10 seconds in ms

unsigned long lastUpdate = 0;
unsigned long lastTrack = 0;
const unsigned long updateInterval = 200; // ms per update

// Slew speed
const float slewSpeedDegPerSec = 2.0;
float stepPerUpdate = slewSpeedDegPerSec * (updateInterval / 1000.0);

// Sidereal tracking
const float raSiderealRate = 1.0 / 3600.0; // h/sec

// LED
const int LED_PIN = 13;
unsigned long lastBlink = 0;
bool ledState = false;

void setup() {
  Serial.begin(9600);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
}

void loop() {
  handleSerial();

  unsigned long now = millis();

  // Check if we should start slewing after delay
  if (slewPending && (now - slewCommandTime >= slewDelay)) {
    slewing = true;
    slewPending = false;
  }

  // Slewing
  if (slewing && now - lastUpdate >= updateInterval) {
    lastUpdate = now;
    slewing = !updateGreatCircle(current, target, stepPerUpdate);
  }

  // Sidereal tracking
  if (!slewing && !slewPending && now - lastTrack >= 1000) {
    lastTrack = now;
    current.ra += raSiderealRate;
    if (current.ra >= 24.0) current.ra -= 24.0;
  }

  // LED update
  updateLED(now);
}

void updateLED(unsigned long now) {
  if (slewPending) {
    // Slow blink during delay (1s)
    if (now - lastBlink > 1000) {
      lastBlink = now;
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
    }
  } 
  else if (slewing) {
    // Fast blink during active slew (300ms)
    if (now - lastBlink > 300) {
      lastBlink = now;
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
    }
  } 
  else {
    // Tracking
    digitalWrite(LED_PIN, HIGH);
  }
}


// Serial command handling
void handleSerial() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '#') {
      handleCommand(inputBuffer);
      inputBuffer = "";
    } else {
      inputBuffer += c;
    }
  }
}

void handleCommand(String cmd) {
  cmd.trim();
  if (cmd == ":GR") { Serial.print(formatRA(current.ra)); Serial.print('#'); }
  else if (cmd == ":GD") { Serial.print(formatDEC(current.dec)); Serial.print('#'); }
  else if (cmd.startsWith(":Sr")) { target.ra = parseRA(cmd.substring(3)); Serial.print('1'); Serial.print('#'); }
  else if (cmd.startsWith(":Sd")) { target.dec = parseDEC(cmd.substring(3)); Serial.print('1'); Serial.print('#'); }
  else if (cmd == ":MS") { 
    slewCommandTime = millis();
    slewPending = true;  // start 10-second delay
    Serial.print('0'); Serial.print('#'); 
  }
  else if (cmd == ":CM") { current = target; Serial.print("Coordinates matched.#"); }
  else if (cmd == ":GW") { Serial.print("Arduino LX200 Emulator#"); }
  else if (cmd == ":GVN") { Serial.print("2.1#"); }
  else if (cmd.startsWith(":Q")) { slewing = false; slewPending = false; Serial.print('#'); }
  else { Serial.print('#'); }
}

// ------------------------ Great-circle update ------------------------
bool updateGreatCircle(Coord &cur, Coord tgt, float maxStepDeg) {
  float raCurRad = radians(cur.ra * 15.0);
  float decCurRad = radians(cur.dec);
  float raTgtRad = radians(tgt.ra * 15.0);
  float decTgtRad = radians(tgt.dec);

  float x0 = cos(decCurRad) * cos(raCurRad);
  float y0 = cos(decCurRad) * sin(raCurRad);
  float z0 = sin(decCurRad);

  float x1 = cos(decTgtRad) * cos(raTgtRad);
  float y1 = cos(decTgtRad) * sin(raTgtRad);
  float z1 = sin(decTgtRad);

  float dot = x0*x1 + y0*y1 + z0*z1;
  if(dot > 1.0) dot = 1.0;
  if(dot < -1.0) dot = -1.0;
  float angle = acos(dot); // radians

  if (angle < radians(0.05)) { cur = tgt; return true; }

  float stepRad = radians(maxStepDeg);
  float t = stepRad / angle;
  if(t > 1.0) t = 1.0;

  float x = (1-t)*x0 + t*x1;
  float y = (1-t)*y0 + t*y1;
  float z = (1-t)*z0 + t*z1;
  float len = sqrt(x*x + y*y + z*z);
  x /= len; y /= len; z /= len;

  cur.dec = degrees(asin(z));
  cur.ra = degrees(atan2(y, x)) / 15.0;
  if(cur.ra < 0.0) cur.ra += 24.0;

  return false;
}

// ------------------------ Formatting / Parsing ------------------------
String formatRA(float raHours) {
  int h = int(raHours);
  int m = int((raHours - h) * 60);
  int s = int((((raHours - h) * 60) - m) * 60);
  char buf[12]; sprintf(buf, "%02d:%02d:%02d", h, m, s); return String(buf);
}

String formatDEC(float decDeg) {
  char sign = (decDeg >= 0) ? '+' : '-';
  decDeg = abs(decDeg);
  int d = int(decDeg);
  int m = int((decDeg - d) * 60);
  int s = int((((decDeg - d) * 60) - m) * 60);
  char buf[14]; sprintf(buf, "%c%02d*%02d:%02d", sign, d, m, s); return String(buf);
}

float parseRA(String str) {
  int h=0,m=0,s=0; sscanf(str.c_str(),"%d:%d:%d",&h,&m,&s);
  return h + m/60.0 + s/3600.0;
}

float parseDEC(String str) {
  char sign='+'; int d=0,m=0,s=0; sscanf(str.c_str(),"%c%d*%d:%d",&sign,&d,&m,&s);
  float val = d + m/60.0 + s/3600.0;
  if(sign=='-') val=-val;
  return val;
}
