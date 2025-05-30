#define ST77XX_DARKGREY 0x7BEF

#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <Wire.h>
#include <RTClib.h>

#define TFT_CS    10
#define TFT_DC    9
#define TFT_RST   8
#define RELAY_PIN 7
#define BTN_P1    2
#define BTN_P2    3
#define BTN_P3    4
#define AUTO_PIN  5

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
RTC_DS3231 rtc;

uint8_t setState = 0;
DateTime currentTime;
uint8_t heureDebut = 0, minuteDebut = 0;
uint8_t tempEau = 20;
uint16_t dureeFiltration = 60;
bool modeAuto = false;
String titre = "";

unsigned long lastDisplayUpdate = 0;
const unsigned long displayInterval = 60000;

int prevHeure = -1, prevMinute = -1;
int prevHeureDebut = -1, prevMinuteDebut = -1;
int prevTempEau = -1, prevDureeFiltration = -1;
bool prevModeAuto = false;
String prevTitre = "";

bool prevBtnP1 = false, prevBtnP2 = false, prevBtnP3 = false;
unsigned long p2PressStart = 0;
unsigned long lastP2Repeat = 0;
const unsigned long longPressDelay = 600;
const unsigned long repeatDelay = 200;
bool p2LongActive = false;

bool showMessage = false;
unsigned long messageTimer = 0;
const unsigned long MESSAGE_DURATION = 1000;

// --- Icônes ---
void afficherIconeHorloge(int x, int y, uint16_t col) {
  tft.drawCircle(x, y, 10, col);
  tft.drawLine(x, y, x, y-7, col);
  tft.drawLine(x, y, x+6, y, col);
}
void afficherIconeSablier(int x, int y, uint16_t col) {
  tft.drawLine(x-7, y-10, x+7, y-10, col);
  tft.drawLine(x-7, y+10, x+7, y+10, col);
  tft.drawLine(x-7, y-10, x+7, y+10, col);
  tft.drawLine(x+7, y-10, x-7, y+10, col);
  tft.drawLine(x-3, y-4, x+3, y+4, col);
  tft.drawLine(x, y+4, x, y+8, col);
}
void afficherIconeThermometre(int x, int y, uint16_t col) {
  tft.drawRect(x-3, y-12, 6, 16, col);
  tft.drawCircle(x, y+7, 7, col);
  tft.fillCircle(x, y+7, 5, col);
  tft.drawLine(x, y-12, x, y+7, col);
}
void afficherFiltrationProgressBar(int x, int y, int w, int h, int total, int courant) {
  tft.fillRect(x, y, w, h, ST77XX_DARKGREY); // Fond gris foncé
  tft.drawRect(x, y, w, h, ST77XX_WHITE);
  tft.drawRect(x+1, y+1, w-2, h-2, ST77XX_WHITE);
  int filled = (total > 0) ? (w-4) * courant / total : 0;
  if(filled > 0)
    tft.fillRect(x+2, y+2, filled, h-4, ST77XX_GREEN);
}

void setup() {
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  pinMode(BTN_P1, INPUT_PULLUP);
  pinMode(BTN_P2, INPUT_PULLUP);
  pinMode(BTN_P3, INPUT_PULLUP);
  pinMode(AUTO_PIN, INPUT_PULLUP);

  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);

  Wire.begin();
  rtc.begin();

  if (!rtc.begin()) {
    tft.setCursor(0,0);
    tft.println("RTC error!");
    while (1);
  }
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
}

void loop() {
  bool btnP1 = !digitalRead(BTN_P1);
  bool btnP2 = !digitalRead(BTN_P2);
  bool btnP3 = !digitalRead(BTN_P3);
  modeAuto = !digitalRead(AUTO_PIN);

  bool bpP1Edge = btnP1 && !prevBtnP1;
  bool bpP2Edge = btnP2 && !prevBtnP2;
  bool bpP3Edge = btnP3 && !prevBtnP3;

  if (btnP2 && !prevBtnP2) {
    p2PressStart = millis();
    p2LongActive = false;
    lastP2Repeat = millis();
  }
  if (btnP2 && !p2LongActive && (millis() - p2PressStart > longPressDelay)) {
    p2LongActive = true;
    lastP2Repeat = millis();
  }
  if (!btnP2) {
    p2LongActive = false;
  }

  // --- En entrant en mode réglage, initialise heureDebut à l'heure courante et dureeFiltration à 0 ---
  static bool wasInReglage = false;
  if ((setState == 1 || setState == 2 || setState == 3 || setState == 4) && !wasInReglage) {
    DateTime now = rtc.now();
    heureDebut = now.hour();
    minuteDebut = now.minute();
    dureeFiltration = 0;
    wasInReglage = true;
  }
  if (setState == 0) wasInReglage = false;

  if (bpP3Edge) {
    setState = 1;
    DateTime now = rtc.now();
    heureDebut = now.hour();
    minuteDebut = now.minute();
    dureeFiltration = 0;
    tempEau = 20;
    titre = "RAZ !";
    delay(200);
    prevBtnP1 = btnP1; prevBtnP2 = btnP2; prevBtnP3 = btnP3;
    return;
  }

  static uint8_t previousSetState = 0;

  // --- PHASE DE REGLAGE : durée à 0 et barre à 0 ---
  uint16_t dureeAffichee = dureeFiltration;
  int courantAffiche = -1; // -1 = calcul normal
  if (setState == 3) {
    dureeAffichee = 0;
    courantAffiche = 0;
  }

  switch (setState) {
    case 0:
      currentTime = rtc.now();
      titre = "";
      gererFiltration();
      if (bpP1Edge) {
        setState = 1;
        delay(200);
      }
      break;
    case 1: // Heure courante
      titre = "Reglage heure";
      if ((bpP2Edge && !p2LongActive) || (btnP2 && p2LongActive && (millis() - lastP2Repeat > repeatDelay))) {
        currentTime = rtc.now() + TimeSpan(0, 0, p2LongActive ? 10 : 1, 0);
        rtc.adjust(currentTime);
        lastP2Repeat = millis();
        delay(40);
      }
      if (bpP1Edge) {
        setState = 2;
        delay(200);
      }
      break;
    case 2: // Début filtration
      titre = "Regl. debut";
      if ((bpP2Edge && !p2LongActive) || (btnP2 && p2LongActive && (millis() - lastP2Repeat > repeatDelay))) {
        uint8_t step = p2LongActive ? 10 : 1;
        int totalMinutes = heureDebut * 60 + minuteDebut + step;
        heureDebut = (totalMinutes / 60) % 24;
        minuteDebut = totalMinutes % 60;
        lastP2Repeat = millis();
        delay(40);
      }
      if (bpP1Edge) {
        setState = 3;
        delay(200);
      }
      break;
    case 3: // Durée filtration
      titre = "Regl. duree";
      if ((bpP2Edge && !p2LongActive) || (btnP2 && p2LongActive && (millis() - lastP2Repeat > repeatDelay))) {
        uint16_t step = p2LongActive ? 10 : 1;
        dureeFiltration += step;
        if (dureeFiltration > 1440) dureeFiltration = 1;
        lastP2Repeat = millis();
        delay(40);
      }
      if (bpP1Edge) {
        setState = 4;
        delay(200);
      }
      break;
    case 4: // Température
      titre = "Regl. temp";
      if ((bpP2Edge && !p2LongActive) || (btnP2 && p2LongActive && (millis() - lastP2Repeat > repeatDelay))) {
        tempEau = (tempEau + (p2LongActive ? 5 : 1)) % 41;
        lastP2Repeat = millis();
        delay(40);
      }
      if (bpP1Edge) {
        setState = 0;
        showMessage = true;
        messageTimer = millis();
        delay(200);
      }
      break;
  }

  previousSetState = setState;

  DateTime now = rtc.now();
  bool needUpdate = false;
  if (now.hour() != prevHeure || now.minute() != prevMinute) needUpdate = true;
  if (heureDebut != prevHeureDebut || minuteDebut != prevMinuteDebut) needUpdate = true;
  if (tempEau != prevTempEau) needUpdate = true;
  if (dureeFiltration != prevDureeFiltration) needUpdate = true;
  if (modeAuto != prevModeAuto) needUpdate = true;
  if (titre != prevTitre) needUpdate = true;
  if (millis() - lastDisplayUpdate > displayInterval) needUpdate = true;

  if (showMessage) {
    afficherMessageTemporaire("Parametres ok !");
    if (millis() - messageTimer > MESSAGE_DURATION) {
      showMessage = false;
      needUpdate = true;
    }
  } else if (needUpdate) {
    afficherEcran(titre, setState, dureeAffichee, courantAffiche);
    prevHeure = now.hour();
    prevMinute = now.minute();
    prevHeureDebut = heureDebut;
    prevMinuteDebut = minuteDebut;
    prevTempEau = tempEau;
    prevDureeFiltration = dureeFiltration;
    prevModeAuto = modeAuto;
    prevTitre = titre;
    lastDisplayUpdate = millis();
  }

  prevBtnP1 = btnP1;
  prevBtnP2 = btnP2;
  prevBtnP3 = btnP3;
}

void afficherEcran(String titre, uint8_t stepState, uint16_t dureeAffichee, int courantAffiche) {
  tft.fillScreen(ST77XX_BLACK);

  // Bandeau mode auto
  tft.fillRect(0, 0, tft.width(), 16, modeAuto ? ST77XX_GREEN : ST77XX_RED);
  tft.setTextSize(1);
  if (modeAuto) {
    tft.setTextColor(ST77XX_BLACK, ST77XX_GREEN);
  } else {
    tft.setTextColor(ST77XX_WHITE, ST77XX_RED);
  }
  tft.setCursor(2, 3);
  tft.print("mode auto : ");
  tft.print(modeAuto ? "on" : "off");

  DateTime now = rtc.now();
  char buf[12];

  // Heure courante (gros, centré haut)
  tft.setTextSize(3);
  tft.setTextColor((stepState==1)?ST77XX_YELLOW:ST77XX_CYAN, ST77XX_BLACK);
  sprintf(buf, "%02d:%02d", now.hour(), now.minute());
  int16_t x1, y1;
  uint16_t w, h;
  tft.getTextBounds(buf, 0, 0, &x1, &y1, &w, &h);
  int xHeure = (tft.width() - w) / 2;
  tft.setCursor(xHeure, 21);
  tft.print(buf);

  // Positions X bien réparties sur 160px (ou 128px si écran plus petit)
  int xDebut = 31;
  int xDuree = 84;
  int xEau   = 136;
  int blocY = 65;

  // Bloc début filtration (gauche) - EN VERT
  afficherIconeHorloge(xDebut, blocY, ST77XX_GREEN);
  tft.setTextSize(1);
  tft.setCursor(xDebut-14, blocY+14);
  tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  tft.print("Debut");
  tft.setCursor(xDebut-18, blocY+27);
  tft.setTextColor((stepState==2)?ST77XX_YELLOW:ST77XX_WHITE, ST77XX_BLACK);
  sprintf(buf, "%02d:%02d", heureDebut, minuteDebut);
  tft.print(buf);

  // Bloc durée filtration (centre) - EN ORANGE
  uint16_t orange = 0xFD20; // code couleur orange (R=255,G=165,B=0)
  afficherIconeSablier(xDuree, blocY, orange);
  tft.setTextColor(orange, ST77XX_BLACK);
  tft.setCursor(xDuree-14, blocY+14);
  tft.print("Duree");
  tft.setCursor(xDuree-18, blocY+27);
  tft.setTextColor((stepState==3)?ST77XX_YELLOW:ST77XX_WHITE, ST77XX_BLACK);
  sprintf(buf, "%02d:%02d", dureeFiltration/60, dureeFiltration%60);
  tft.print(buf);

  // Bloc température eau (droite) - EN BLEU
  afficherIconeThermometre(xEau, blocY-2, ST77XX_BLUE);
  tft.setTextColor(ST77XX_BLUE, ST77XX_BLACK);
  tft.setCursor(xEau-9, blocY+14);
  tft.print("Eau");
  tft.setCursor(xEau-7, blocY+27);
  tft.setTextColor((stepState==4)?ST77XX_YELLOW:ST77XX_WHITE, ST77XX_BLACK);
  sprintf(buf, "%02dC", tempEau);
  tft.print(buf);

  // --- Barre de progression filtration ---
  // Largeur maximale (bord à bord avec marge de 4px), hauteur réduite à 10px
  int total = dureeAffichee;
  int courant = courantAffiche;
  if (courant < 0) { // Si ce n'est pas une valeur forcée par le mode réglage
    int debutMinutes = heureDebut * 60 + minuteDebut;
    int nowMinutes = now.hour() * 60 + now.minute();
    int finMinutes = (debutMinutes + total) % (24*60);
    courant = 0;
    if (total > 0) {
      if (finMinutes > debutMinutes) {
        if (nowMinutes >= debutMinutes && nowMinutes < finMinutes)
          courant = nowMinutes - debutMinutes;
        else if (nowMinutes >= finMinutes)
          courant = total;
        else
          courant = 0;
      } else {
        if (nowMinutes >= debutMinutes)
          courant = nowMinutes - debutMinutes;
        else if (nowMinutes < finMinutes)
          courant = (24*60 - debutMinutes) + nowMinutes;
        else
          courant = 0;
      }
      if (courant < 0) courant = 0;
      if (courant > total) courant = total;
    }
  }
  // Largeur maximale (bord à bord avec marge de 4px), hauteur réduite à 10px, position Y=106
  int bar_x = 4;
  int bar_y = 106;
  int bar_w = tft.width() - 8;
  int bar_h = 10;
  afficherFiltrationProgressBar(bar_x, bar_y, bar_w, bar_h, total, courant);

  // Message ou titre en bas
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  if (titre.length() > 0) {
    tft.fillRect(0, 146, tft.width(), 13, ST77XX_BLUE);
    tft.setCursor(5, 149);
    tft.setTextColor(ST77XX_YELLOW, ST77XX_BLUE);
    tft.print(titre);
  }
  tft.setTextSize(1);
}

void afficherMessageTemporaire(const char* msg) {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(ST77XX_CYAN);
  int16_t x1, y1;
  uint16_t w, h;
  tft.setTextSize(1);
  tft.getTextBounds(msg, 0, 0, &x1, &y1, &w, &h);
  tft.setCursor((tft.width() - w) / 2, (tft.height() - h) / 2);
  tft.println(msg);
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
}

void gererFiltration() {
  if (modeAuto) {
    dureeFiltration = calculFiltrationAuto(tempEau) * 60;
  }
  DateTime now = rtc.now();
  int debutMinutes = heureDebut * 60 + minuteDebut;
  int nowMinutes = now.hour() * 60 + now.minute();
  int finMinutes = (debutMinutes + dureeFiltration) % (24*60);

  bool enFiltration;
  if (finMinutes > debutMinutes) {
    enFiltration = (nowMinutes >= debutMinutes) && (nowMinutes < finMinutes);
  } else {
    enFiltration = (nowMinutes >= debutMinutes) || (nowMinutes < finMinutes);
  }

  if (enFiltration) {
    digitalWrite(RELAY_PIN, HIGH);
  } else {
    digitalWrite(RELAY_PIN, LOW);
  }
}

uint8_t calculFiltrationAuto(uint8_t temp) {
  if (temp < 0) return 24;
  else if (temp >= 30) return 24;
  else if (temp >= 28) return 21;
  else if (temp >= 26) return 19;
  else if (temp >= 24) return 16;
  else if (temp >= 21) return 14;
  else if (temp >= 18) return 12;
  else if (temp >= 15) return 8;
  else if (temp >= 10) return 5;
  else if (temp >= 6) return 3;
  else if (temp >= 3) return 1;
  else return 24;
}