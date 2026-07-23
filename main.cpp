#include <M5Cardputer.h>
#include <Wire.h>
#define W 120
#define H 67

M5Canvas canvas(&M5Cardputer.Display);

int16_t bufR1[W * H] = {0}, bufR2[W * H] = {0};
int16_t bufG1[W * H] = {0}, bufG2[W * H] = {0};
int16_t bufB1[W * H] = {0}, bufB2[W * H] = {0};

int16_t *pR1 = bufR1, *pR2 = bufR2;
int16_t *pG1 = bufG1, *pG2 = bufG2;
int16_t *pB1 = bufB1, *pB2 = bufB2;

bool prevPressed[256] = {false};

// TCA8418(ADVのキーボードIC)のINTピン。ライブラリ既定値と同じ
#define KB_INT_PIN 11
uint32_t intLowSince = 0; // INTピンがLOWになり続けている開始時刻(0=HIGH)

// Push Any Key表示用の変数
bool hasPressedAnyKey = false;
uint32_t lastKeyPressTime = 0;

const int pentatonic[] = {261, 293, 329, 392, 440, 523, 587, 659, 783, 880};

// キー名ポップアップ表示用の構造体と配列
#define MAX_POPUPS 5
struct Popup {
    bool active;
    String text;
    int px, py;
    uint32_t startTime;
    bool isNormal; // 通常キーかどうかのフラグ
};

Popup popups[MAX_POPUPS];

void resetI2CBus() {
    pinMode(SDA, OUTPUT_OPEN_DRAIN);
    pinMode(SCL, OUTPUT_OPEN_DRAIN);

    for (int i = 0; i < 9; i++) {
        digitalWrite(SCL, LOW); delayMicroseconds(5);
        digitalWrite(SCL, HIGH); delayMicroseconds(5);
    }

    digitalWrite(SDA, LOW); delayMicroseconds(5);
    digitalWrite(SDA, HIGH);
    Wire.end();
    Wire.begin(SDA, SCL);
}

void setup() {
    auto cfg = M5.config();
    M5Cardputer.begin(cfg);
    M5Cardputer.Speaker.setVolume(180);
    canvas.setColorDepth(16);
    canvas.createSprite(W, H);
}

void updateWave(int16_t* b1, int16_t* b2) {
    int16_t *ptr1 = b1 + W + 1;
    int16_t *ptr2 = b2 + W + 1;

    for (int y = 1; y < H - 1; y++) {
        for (int x = 1; x < W - 1; x++) {
            *ptr2 = ((*(ptr1 - 1) + *(ptr1 + 1) + *(ptr1 - W) + *(ptr1 + W)) >> 1) - *ptr2;
            *ptr2 -= *ptr2 >> 5;
            ptr1++;
            ptr2++;
        }
        ptr1 += 2; // 両端のピクセルをスキップ
        ptr2 += 2;
    }
}

void injectDrop(int px, int py, int force) {
    int rCol = random(0, 2), gCol = random(0, 2), bCol = random(0, 2);

    if (rCol == 0 && gCol == 0 && bCol == 0) { rCol = 1; gCol = 1; bCol = 1; }

    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int x = px + dx, y = py + dy;
            if (x > 0 && x < W - 1 && y > 0 && y < H - 1) {
                int i = y * W + x;
                if (rCol) pR1[i] = force;
                if (gCol) pG1[i] = force;
                if (bCol) pB1[i] = force;
            }
        }
    }
    M5Cardputer.Speaker.tone(pentatonic[random(0, 10)], 50);
}

void loop() {
    // 描画と計算は高速に回し続ける
    updateWave(pR1, pR2); updateWave(pG1, pG2); updateWave(pB1, pB2);
    int16_t *temp;
    temp = pR1; pR1 = pR2; pR2 = temp;
    temp = pG1; pG1 = pG2; pG2 = temp;
    temp = pB1; pB1 = pB2; pB2 = temp;

    uint16_t *img_buf = (uint16_t *)canvas.getBuffer();
    int16_t *rPtr = pR2;
    int16_t *gPtr = pG2;
    int16_t *bPtr = pB2;

    for (int i = 0; i < W * H; i++) {
        int r = *rPtr++; if (r < 0) r = -r; if (r > 255) r = 255;
        int g = *gPtr++; if (g < 0) g = -g; if (g > 255) g = 255;
        int b = *bPtr++; if (b < 0) b = -b; if (b > 255) b = 255;

        uint16_t color = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);

        *img_buf++ = (color >> 8) | (color << 8); // エンディアン反転
    }

    // 波紋を描画した直後にポップアップ文字を上書き
    canvas.setTextDatum(middle_center);

    // 電源ON直後、または15秒間無操作の時にメッセージを表示
    if (!hasPressedAnyKey || (millis() - lastKeyPressTime > 15000)) {
        canvas.setFont(&fonts::lgfxJapanGothic_16); // 120px幅に収まるサイズに縮小
        canvas.setTextSize(1);
        canvas.setTextColor(canvas.color888(255, 255, 255));
        canvas.drawString("Push Any Key", W / 2, H / 2);
    }

    for (int p = 0; p < MAX_POPUPS; p++) {
        if (popups[p].active) {
            uint32_t elapsed = millis() - popups[p].startTime;

            if (elapsed > 2000) { // 消える時間を2秒(2000ms)に設定
                popups[p].active = false;
            } else {
                // 通常キーならサイズ24、特殊キーならサイズ16の滑らかなフォントを直接指定
                if (popups[p].text == "\\") {
                    canvas.setFont(&fonts::DejaVu24); // \を円記号化させないための欧文フォント
                } else if (popups[p].isNormal) {
                    canvas.setFont(&fonts::lgfxJapanGothic_24);
                } else {
                    canvas.setFont(&fonts::lgfxJapanGothic_16);
                }

                canvas.setTextSize(1); // 倍率での引き延ばしは行わない

                // 右下(各2ピクセルずらす)に白色で影を描画
                canvas.setTextColor(canvas.color888(255, 255, 255));
                canvas.drawString(popups[p].text, popups[p].px + 2, popups[p].py + 2);

                // その後、元の位置に黒色でメインの文字を描画
                canvas.setTextColor(canvas.color888(0, 0, 0));
                canvas.drawString(popups[p].text, popups[p].px, popups[p].py);
            }
        }
    }

    canvas.pushRotateZoom(120, 67.5, 0, 2.0, 2.0);

    // --- キーボード読み取り ---
    // ライブラリのTCA8418リーダーはupdate()1回につきFIFOイベントを1個しか消費しない。
    // 30msに1回だと同時押し連打でFIFO(10段)が溢れてイベントが消失するため、
    // 毎フレーム複数回呼んでFIFOを空にする(イベントが無ければ即returnなのでコストほぼゼロ)
    for (int i = 0; i < 10; i++) M5Cardputer.Keyboard.updateKeyList();
    M5Cardputer.update();

    // --- 復旧ウォッチドッグ ---
    // ライブラリ内部の_isr_flagには競合があり、INT_STATクリア直後に新イベントが来ると
    // フラグが false で上書きされ、INTピンがLOWのまま二度と割り込みが来なくなる(=キー入力死亡)。
    // INTピンが500ms以上LOWのままなら詰まったと判断してリーダーを作り直す
    if (digitalRead(KB_INT_PIN) == LOW) {
        if (intLowSince == 0) {
            intLowSince = millis();
        } else if (millis() - intLowSince > 500) {
            detachInterrupt(digitalPinToInterrupt(KB_INT_PIN)); // 旧リーダー破棄中の割り込みを防ぐ
            M5Cardputer.Keyboard.begin(); // リーダー再生成→FIFOフラッシュ→INT再アーム
            intLowSince = 0;
        }
    } else {
        intLowSince = 0;
    }

    {
        auto state = M5Cardputer.Keyboard.keysState();

        bool currentPressed[256] = {false};
        for (auto k : state.word) {
            uint8_t key_code = (uint8_t)k;

            if (key_code >= 'A' && key_code <= 'Z') key_code = key_code - 'A' + 'a';
            else {
                switch (key_code) {
                    case '!': key_code = '1'; break; case '@': key_code = '2'; break; case '#': key_code = '3'; break;
                    case '$': key_code = '4'; break; case '%': key_code = '5'; break; case '^': key_code = '6'; break;
                    case '&': key_code = '7'; break; case '*': key_code = '8'; break; case '(': key_code = '9'; break;
                    case ')': key_code = '0'; break; case '_': key_code = '-'; break; case '+': key_code = '='; break;
                    case '{': key_code = '['; break; case '}': key_code = ']'; break; case '|': key_code = '\\'; break;
                    case ':': key_code = ';'; break; case '"': key_code = '\''; break; case '<': key_code = ','; break;
                    case '>': key_code = '.'; break; case '?': key_code = '/'; break; case '~': key_code = '`'; break;
                }
            }

            currentPressed[key_code] = true;
        }

        if (state.ctrl) currentPressed[128] = true;
        if (state.shift) currentPressed[129] = true;
        if (state.fn) currentPressed[130] = true;
        if (state.alt) currentPressed[131] = true;
        if (state.opt) currentPressed[132] = true;
        if (state.tab) currentPressed[9] = true;
        if (state.enter) currentPressed['\n'] = true;
        if (state.del) currentPressed[8] = true;

        for (int k = 0; k < 256; k++) {
            if (currentPressed[k] && !prevPressed[k]) {
                hasPressedAnyKey = true;
                lastKeyPressTime = millis();

                int px = -1, py = -1;
                if(k==27 || k=='`' || k=='~') {px=4; py=10;}
                else if(k=='1' || k=='!') {px=12; py=10;} else if(k=='2' || k=='@') {px=20; py=10;} else if(k=='3' || k=='#') {px=28; py=10;}
                else if(k=='4' || k=='$') {px=36; py=10;} else if(k=='5' || k=='%') {px=44; py=10;} else if(k=='6' || k=='^') {px=52; py=10;}
                else if(k=='7' || k=='&') {px=60; py=10;} else if(k=='8' || k=='*') {px=68; py=10;} else if(k=='9' || k=='(') {px=76; py=10;}
                else if(k=='0' || k==')') {px=84; py=10;} else if(k=='-' || k=='_') {px=92; py=10;} else if(k=='=' || k=='+') {px=100; py=10;}
                else if(k==8) {px=108; py=10;}
                else if(k==9) {px=6; py=24;}
                else if(k=='q' || k=='Q') {px=14; py=24;} else if(k=='w' || k=='W') {px=22; py=24;} else if(k=='e' || k=='E') {px=30; py=24;}
                else if(k=='r' || k=='R') {px=38; py=24;} else if(k=='t' || k=='T') {px=46; py=24;} else if(k=='y' || k=='Y') {px=54; py=24;}
                else if(k=='u' || k=='U') {px=62; py=24;} else if(k=='i' || k=='I') {px=70; py=24;} else if(k=='o' || k=='O') {px=78; py=24;}
                else if(k=='p' || k=='P') {px=86; py=24;} else if(k=='[' || k=='{') {px=94; py=24;} else if(k==']' || k=='}') {px=102; py=24;}
                else if(k=='\\' || k=='|') {px=110; py=24;}
                else if(k==130) {px=6; py=38;}
                else if(k==129) {px=14; py=38;}
                else if(k=='a' || k=='A') {px=22; py=38;} else if(k=='s' || k=='S') {px=30; py=38;} else if(k=='d' || k=='D') {px=38; py=38;}
                else if(k=='f' || k=='F') {px=46; py=38;} else if(k=='g' || k=='G') {px=54; py=38;} else if(k=='h' || k=='H') {px=62; py=38;}
                else if(k=='j' || k=='J') {px=70; py=38;} else if(k=='k' || k=='K') {px=78; py=38;} else if(k=='l' || k=='L') {px=86; py=38;}
                else if(k==';' || k==':') {px=94; py=38;} else if(k=='\'' || k=='"') {px=102; py=38;} else if(k=='\n' || k==13) {px=110; py=38;}
                else if(k==128) {px=6; py=52;}
                else if(k==132) {px=14; py=52;}
                else if(k==131) {px=22; py=52;}
                else if(k=='z' || k=='Z') {px=30; py=52;} else if(k=='x' || k=='X') {px=38; py=52;} else if(k=='c' || k=='C') {px=46; py=52;}
                else if(k=='v' || k=='V') {px=54; py=52;} else if(k=='b' || k=='B') {px=62; py=52;} else if(k=='n' || k=='N') {px=70; py=52;}
                else if(k=='m' || k=='M') {px=78; py=52;} else if(k==',' || k=='<') {px=86; py=52;} else if(k=='.' || k=='>') {px=94; py=52;}
                else if(k=='/' || k=='?') {px=102; py=52;}
                else if(k==' ') {px=58; py=62;}

                if (px != -1 && py != -1) {
                    injectDrop(px, py, 6000);

                    // スペースキーは無視し、10回に1回の確率で発動
                    if (k != ' ' && random(0, 10) == 0) {
                        String keyName = "";
                        bool isNormal = false; // 通常の文字キーかどうかの判定用

                        // 左上のキー(`や~)も含めてESCとして扱う
                        if (k == 27 || k == '`' || k == '~') keyName = "ESC";
                        else if (k == 8) keyName = "DEL";
                        else if (k == 9) keyName = "TAB";
                        else if (k == 130) keyName = "FN";
                        else if (k == 129) keyName = "Aa";
                        else if (k == '\n' || k == 13) keyName = "Enter";
                        else if (k == 128) keyName = "CTRL";
                        else if (k == 132) keyName = "OPT";
                        else if (k == 131) keyName = "ALT";
                        else if (k >= 33 && k <= 126) {
                            keyName = String((char)toupper(k));
                            isNormal = true; // 通常キーとしてフラグを立てる
                        }

                        if (keyName != "") {
                            for (int p = 0; p < MAX_POPUPS; p++) {
                                if (!popups[p].active) {
                                    popups[p].active = true;
                                    popups[p].text = keyName;
                                    popups[p].isNormal = isNormal;

                                    // 画面端の見切れ対策として描画X座標を補正
                                    int drawX = px;
                                    if (px < 30 && keyName.length() > 1) drawX = px + 8;
                                    if (keyName == "Enter") drawX = px - 12; // Enterのみずらす
                                    if (keyName == "CTRL") drawX += 4;      // 前回の+6から2ピクセル外側へ
                                    if (keyName == "FN") drawX -= 6;        // さらに半文字分外側へ

                                    popups[p].px = drawX;
                                    popups[p].py = py;
                                    popups[p].startTime = millis();
                                    break; // 空きスロットが見つかったら登録して抜ける
                                }
                            }
                        }
                    }
                }
            }
            prevPressed[k] = currentPressed[k];
        }
    }
}