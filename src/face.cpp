// =============================================================
//  face.cpp — Motor de cara y expresiones para espToy
//  Plataforma : Seeed XIAO ESP32-S3
//  Display    : OLED SSD1309 128×64, librería U8g2
//
//  Rediseño v2: cada expresión tiene un EyeStyle por ojo.
//  Los estilos cerrados/especiales no usan el rect+párpados
//  sino rutinas de dibujo propias (arcos gruesos, ángulos, etc.)
// =============================================================

#include "face.h"
#include "config.h"   // EASING_EXPRESION, EASING_MIRADA, PARPADEO_*, MIRADA_*
#include <math.h>     // sinf, cosf

// ----------------------------------------------------------------
//  Instancia global
// ----------------------------------------------------------------
Face face;

// ----------------------------------------------------------------
//  EyeStyle: tipo de dibujo para cada ojo
// ----------------------------------------------------------------
enum class EyeStyle : uint8_t {
    RECT,          // rectángulo redondeado con párpados (comportamiento original)
    ARCO_ARRIBA,   // "^^"  — semicírculo con abertura hacia abajo (FELIZ, mitad superior)
    ARCO_ABAJO,    // "‿‿"  — semicírculo con abertura hacia arriba (DORMIDO, mitad inferior)
    ANGULO,        // "> <" — triángulo/ángulo apuntando al centro (ENOJADO)
    CIRCULO,       // disco sólido sin pupila (AMOR, SORPRENDIDO)
    CIRCULO_PUPILA,// disco blanco con puntito negro central (SORPRENDIDO diferenciado)
    SLAB,          // barra rectangular delgada e inclinada (SOSPECHOSO — ojo entrecerrado)
    CRUZ,          // X — dos líneas diagonales gruesas (reservado / error)
};

// ----------------------------------------------------------------
//  Definición extendida de expresión
// ----------------------------------------------------------------
struct ExprDef {
    // Ojo izquierdo
    float lPTop, lPBot, lSlopeTop, lSlopeBot;
    EyeStyle lStyle;
    // Ojo derecho
    float rPTop, rPBot, rSlopeTop, rSlopeBot;
    EyeStyle rStyle;
    // Forma compartida
    float w, h, r;
};

// ----------------------------------------------------------------
//  Posiciones fijas de los ojos (centro base)
// ----------------------------------------------------------------
static const float EYE_LEFT_CX  = 38.0f;
static const float EYE_RIGHT_CX = 90.0f;
static const float EYE_CY       = 35.0f;

// ----------------------------------------------------------------
//  Tabla de expresiones
//  Orden: NEUTRAL, FELIZ, TRISTE, ENOJADO, SORPRENDIDO,
//         ABURRIDO, DORMIDO, SOSPECHOSO, AMOR, GUINO
//
//  Los campos pTop/pBot/slope sólo tienen efecto en EyeStyle::RECT.
// ----------------------------------------------------------------
static const ExprDef EXPR_TABLE[10] = {

    // 0 — NEUTRAL: rectángulos redondeados normales
    { 0, 0, 0, 0, EyeStyle::RECT,
      0, 0, 0, 0, EyeStyle::RECT,
      28, 22, 6 },

    // 1 — FELIZ: arcos "^^" gruesos — semicírculo superior, abierto abajo
    { 0, 0, 0, 0, EyeStyle::ARCO_ARRIBA,
      0, 0, 0, 0, EyeStyle::ARCO_ARRIBA,
      28, 14, 0 },

    // 2 — TRISTE: párpados externos muy caídos (rect con pendiente fuerte)
    //   Convenio de slopeTop: valor > 0 → el lado DERECHO del párpado sube
    //   → el lado IZQUIERDO queda más abajo (más tapado).
    //   Para ojo izq: queremos que la esquina EXTERNA (izq) caiga → slopeTop > 0
    //   Para ojo der: queremos que la esquina EXTERNA (der) caiga → slopeTop < 0
    //   (porque la esquina der corresponde a yLidR que baja cuando slopeTop < 0)
    { 5, 0, +8, 0, EyeStyle::RECT,
      5, 0, -8, 0, EyeStyle::RECT,
      26, 20, 5 },

    // 3 — ENOJADO: ángulos ">" y "<" apuntando al centro — triángulos agresivos
    { 0, 0, 0, 0, EyeStyle::ANGULO,
      0, 0, 0, 0, EyeStyle::ANGULO,
      28, 22, 0 },

    // 4 — SORPRENDIDO: círculos grandes con pupila
    { 0, 0, 0, 0, EyeStyle::CIRCULO_PUPILA,
      0, 0, 0, 0, EyeStyle::CIRCULO_PUPILA,
      28, 28, 14 },

    // 5 — ABURRIDO: párpado superior a media asta — rect con pTop alto
    { 10, 2, 0, 0, EyeStyle::RECT,
      10, 2, 0, 0, EyeStyle::RECT,
      28, 20, 6 },

    // 6 — DORMIDO: arcos "‿‿" — semicírculo inferior, abierto arriba + Zzz
    { 0, 0, 0, 0, EyeStyle::ARCO_ABAJO,
      0, 0, 0, 0, EyeStyle::ARCO_ABAJO,
      28, 14, 0 },

    // 7 — SOSPECHOSO: ojo izq = slab inclinado, ojo der = rect entrecerrado
    //   Slab: barra delgada y diagonal (ojo casi cerrado con actitud)
    //   Der: rect normal con pTop para entrecerrar
    { 0, 0, 0, 0, EyeStyle::SLAB,
      7, 0, -3, 0, EyeStyle::RECT,
      28, 22, 5 },

    // 8 — AMOR: círculos grandes llenos (sin pupila), w=h=26
    { 0, 0, 0, 0, EyeStyle::CIRCULO,
      0, 0, 0, 0, EyeStyle::CIRCULO,
      26, 26, 13 },

    // 9 — GUINO: ojo izq cerrado "‿" (arco abajo/feliz), ojo der abierto "^"
    { 0, 0, 0, 0, EyeStyle::ARCO_ABAJO,
      0, 0, 0, 0, EyeStyle::ARCO_ARRIBA,
      28, 14, 0 },
};

// ----------------------------------------------------------------
//  EyeStylePair: estilos activos para los dos ojos
//  Se actualiza instantáneamente al llamar setExpression().
// ----------------------------------------------------------------
static EyeStyle s_leftStyle  = EyeStyle::RECT;
static EyeStyle s_rightStyle = EyeStyle::RECT;

// ----------------------------------------------------------------
//  Helpers de clamping
// ----------------------------------------------------------------
static inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// ----------------------------------------------------------------
//  Carga los EyeParams destino para la expresión dada
// ----------------------------------------------------------------
static void exprToTargets(Expression e,
                          EyeParams &lt, EyeParams &rt)
{
    uint8_t idx = (uint8_t)e;
    if (idx >= 10) idx = 0;
    const ExprDef &d = EXPR_TABLE[idx];

    lt.cx       = EYE_LEFT_CX;
    lt.cy       = EYE_CY;
    lt.w        = d.w;
    lt.h        = d.h;
    lt.r        = d.r;
    lt.pTop     = d.lPTop;
    lt.pBot     = d.lPBot;
    lt.slopeTop = d.lSlopeTop;
    lt.slopeBot = d.lSlopeBot;
    lt.offX     = 0.0f;
    lt.offY     = 0.0f;

    rt.cx       = EYE_RIGHT_CX;
    rt.cy       = EYE_CY;
    rt.w        = d.w;
    rt.h        = d.h;
    rt.r        = d.r;
    rt.pTop     = d.rPTop;
    rt.pBot     = d.rPBot;
    rt.slopeTop = d.rSlopeTop;
    rt.slopeBot = d.rSlopeBot;
    rt.offX     = 0.0f;
    rt.offY     = 0.0f;

    // Estilos instantáneos (sin interpolación)
    s_leftStyle  = d.lStyle;
    s_rightStyle = d.rStyle;
}

// ----------------------------------------------------------------
//  Face::lerpEye  (ease-out: cur += (tgt-cur)*t)
//  offX/offY NO se interpolan aquí; los controla la mirada errante
// ----------------------------------------------------------------
void Face::lerpEye(EyeParams &cur, const EyeParams &tgt, float t)
{
    cur.cx       += (tgt.cx       - cur.cx)       * t;
    cur.cy       += (tgt.cy       - cur.cy)       * t;
    cur.w        += (tgt.w        - cur.w)        * t;
    cur.h        += (tgt.h        - cur.h)        * t;
    cur.r        += (tgt.r        - cur.r)        * t;
    cur.pTop     += (tgt.pTop     - cur.pTop)     * t;
    cur.pBot     += (tgt.pBot     - cur.pBot)     * t;
    cur.slopeTop += (tgt.slopeTop - cur.slopeTop) * t;
    cur.slopeBot += (tgt.slopeBot - cur.slopeBot) * t;
    // offX/offY se manejan por separado
}

// ----------------------------------------------------------------
//  Face::scheduleNextBlink
// ----------------------------------------------------------------
void Face::scheduleNextBlink(uint32_t now)
{
    uint32_t rango = PARPADEO_MAX_MS - PARPADEO_MIN_MS;
    _blinkNextMs = now + PARPADEO_MIN_MS + (uint32_t)(random((long)rango));
}

// ----------------------------------------------------------------
//  Face::scheduleNextGaze
// ----------------------------------------------------------------
void Face::scheduleNextGaze(uint32_t now)
{
    uint32_t rango = MIRADA_MAX_MS - MIRADA_MIN_MS;
    _gazeNextMs = now + MIRADA_MIN_MS + (uint32_t)(random((long)rango));
}

// ----------------------------------------------------------------
//  Face::begin  —  estado inicial NEUTRAL
// ----------------------------------------------------------------
void Face::begin()
{
    _expr = Expression::NEUTRAL;

    exprToTargets(_expr, _leftTgt, _rightTgt);

    _leftCur  = _leftTgt;
    _rightCur = _rightTgt;

    _gazeOffX = 0.0f;
    _gazeOffY = 0.0f;
    _gazeTgtX = 0.0f;
    _gazeTgtY = 0.0f;
    _breathPhase = 0.0f;

    _blinkState = BlinkState::IDLE;
    _blinkFrame = 0;

    uint32_t now = millis();
    scheduleNextBlink(now);
    scheduleNextGaze(now);
}

// ----------------------------------------------------------------
//  Face::setExpression
// ----------------------------------------------------------------
void Face::setExpression(Expression e)
{
    _expr = e;
    exprToTargets(e, _leftTgt, _rightTgt);
}

// ----------------------------------------------------------------
//  Face::expression
// ----------------------------------------------------------------
Expression Face::expression() const
{
    return _expr;
}

// ----------------------------------------------------------------
//  Face::update  —  llamar una vez por frame (now = millis())
// ----------------------------------------------------------------
void Face::update(uint32_t now)
{
    // --- 1. Interpolación expresión (ease-out) -------------------
    lerpEye(_leftCur,  _leftTgt,  EASING_EXPRESION);
    lerpEye(_rightCur, _rightTgt, EASING_EXPRESION);

    // --- 2. Mirada errante --------------------------------------
    if (now >= _gazeNextMs) {
        float rango = MIRADA_RANGO_PX * 2.0f;
        _gazeTgtX = -MIRADA_RANGO_PX + (float)(random((long)(rango * 100))) / 100.0f;
        _gazeTgtY = -MIRADA_RANGO_PX + (float)(random((long)(rango * 100))) / 100.0f;
        scheduleNextGaze(now);
    }
    _gazeOffX += (_gazeTgtX - _gazeOffX) * EASING_MIRADA;
    _gazeOffY += (_gazeTgtY - _gazeOffY) * EASING_MIRADA;

    _leftCur.offX  = _gazeOffX;
    _leftCur.offY  = _gazeOffY;
    _rightCur.offX = _gazeOffX;
    _rightCur.offY = _gazeOffY;

    // --- 3. Micro-movimiento senoidal (respiración) -------------
    _breathPhase += 0.063f;
    if (_breathPhase > 6.2832f) _breathPhase -= 6.2832f;
    float breathOff = sinf(_breathPhase) * 1.0f;
    _leftCur.offY  += breathOff;
    _rightCur.offY += breathOff;

    // --- 4. Parpadeo --------------------------------------------
    // Solo para expresiones con ojos "abiertos" (RECT, CIRCULO*)
    // Los estilos cerrados (ARCO_ABAJO, SLAB, ANGULO) no parpadean.
    bool estiloIzqParpadeaOk = (s_leftStyle  == EyeStyle::RECT ||
                                 s_leftStyle  == EyeStyle::CIRCULO ||
                                 s_leftStyle  == EyeStyle::CIRCULO_PUPILA);
    bool estiloDerParpadeaOk = (s_rightStyle == EyeStyle::RECT ||
                                 s_rightStyle == EyeStyle::CIRCULO ||
                                 s_rightStyle == EyeStyle::CIRCULO_PUPILA);
    bool puedeParpardear = (estiloIzqParpadeaOk || estiloDerParpadeaOk);
    bool dormido = (_expr == Expression::DORMIDO);

    if (!dormido && puedeParpardear) {
        switch (_blinkState) {

        case BlinkState::IDLE:
            if (now >= _blinkNextMs) {
                _blinkState = BlinkState::CLOSING;
                _blinkFrame = 0;
            }
            break;

        case BlinkState::CLOSING:
            {
                float hBase = _leftTgt.h;
                float pCierre = hBase * 0.9f;
                if (estiloIzqParpadeaOk) _leftCur.pTop  = pCierre;
                if (estiloDerParpadeaOk) _rightCur.pTop = pCierre;
                _blinkFrame++;
                if (_blinkFrame >= 2) {
                    _blinkState = BlinkState::CLOSED;
                    _blinkFrame = 0;
                }
            }
            break;

        case BlinkState::CLOSED:
            {
                float hBase = _leftTgt.h;
                if (estiloIzqParpadeaOk) _leftCur.pTop  = hBase;
                if (estiloDerParpadeaOk) _rightCur.pTop = hBase;
                _blinkFrame++;
                if (_blinkFrame >= 1) {
                    _blinkState = BlinkState::OPENING;
                    _blinkFrame = 0;
                }
            }
            break;

        case BlinkState::OPENING:
            if (estiloIzqParpadeaOk) _leftCur.pTop  = _leftTgt.pTop;
            if (estiloDerParpadeaOk) _rightCur.pTop = _rightTgt.pTop;
            _blinkFrame++;
            if (_blinkFrame >= 2) {
                _blinkState = BlinkState::IDLE;
                _blinkFrame = 0;
                scheduleNextBlink(now);
            }
            break;
        }
    }
}

// ================================================================
//  Rutinas de dibujo especializadas por estilo
// ================================================================

// ----------------------------------------------------------------
//  drawEyeArcoArriba — "^^" arco grueso abierto hacia abajo
//
//  Técnica: disco blanco grande → disco negro interior → caja negra
//  que tapa la mitad inferior. Resultado: semicírculo superior grueso.
//
//  w  : ancho del ojo (determina radio externo)
//  grosor: ~5 px
// ----------------------------------------------------------------
static void drawEyeArcoArriba(U8G2 &u8, int16_t cx, int16_t cy, int16_t w)
{
    int16_t rx = w / 2;        // radio externo
    if (rx < 6) rx = 6;
    int16_t grosor = 5;        // grosor del arco en píxeles
    int16_t ri = rx - grosor;  // radio interno (hueco)
    if (ri < 1) ri = 1;

    // El centro del círculo queda en cy+rx/2 para que la cima del arco
    // quede centrada verticalmente en el área del ojo
    int16_t discCy = cy + rx / 2;

    // 1. Disco blanco grande (arco exterior)
    u8.setDrawColor(1);
    u8.drawDisc((u8g2_uint_t)cx, (u8g2_uint_t)discCy, (u8g2_uint_t)rx);

    // 2. Disco negro interior (hueco del arco)
    u8.setDrawColor(0);
    u8.drawDisc((u8g2_uint_t)cx, (u8g2_uint_t)discCy, (u8g2_uint_t)ri);

    // 3. Caja negra que tapa la mitad inferior del disco completo
    //    Desde el centro del círculo hacia abajo (+1 para no dejar borde)
    u8.drawBox((u8g2_uint_t)(cx - rx - 1),
               (u8g2_uint_t)(discCy),
               (u8g2_uint_t)(rx * 2 + 2),
               (u8g2_uint_t)(rx + 4));

    u8.setDrawColor(1);
}

// ----------------------------------------------------------------
//  drawEyeArcoAbajo — "‿‿" arco grueso abierto hacia arriba
//
//  Inverso de ArcoArriba: muestra la mitad inferior del anillo.
// ----------------------------------------------------------------
static void drawEyeArcoAbajo(U8G2 &u8, int16_t cx, int16_t cy, int16_t w)
{
    int16_t rx = w / 2;
    if (rx < 6) rx = 6;
    int16_t grosor = 5;
    int16_t ri = rx - grosor;
    if (ri < 1) ri = 1;

    // Centro del círculo arriba para que la curva quede centrada
    int16_t discCy = cy - rx / 2;

    // 1. Disco blanco
    u8.setDrawColor(1);
    u8.drawDisc((u8g2_uint_t)cx, (u8g2_uint_t)discCy, (u8g2_uint_t)rx);

    // 2. Disco negro interior
    u8.setDrawColor(0);
    u8.drawDisc((u8g2_uint_t)cx, (u8g2_uint_t)discCy, (u8g2_uint_t)ri);

    // 3. Caja negra que tapa la mitad superior
    u8.drawBox((u8g2_uint_t)(cx - rx - 1),
               (u8g2_uint_t)(discCy - rx - 4),
               (u8g2_uint_t)(rx * 2 + 2),
               (u8g2_uint_t)(rx + 4));

    u8.setDrawColor(1);
}

// ----------------------------------------------------------------
//  drawEyeAngulo — "> <" triángulo/ángulo agresivo
//
//  Ojo izquierdo (isLeft=true):  forma ">"  — vértice apunta derecha
//  Ojo derecho   (isLeft=false): forma "<"  — vértice apunta izquierda
//
//  Se dibuja como un polígono relleno usando drawTriangle.
//  Grosor visual: se superponen varias capas desplazadas ±1px.
// ----------------------------------------------------------------
static void drawEyeAngulo(U8G2 &u8, int16_t cx, int16_t cy,
                           int16_t w, int16_t h, bool isLeft)
{
    // Puntos del ángulo:
    //   Ojo izq (">"):  vértice derecho al medio, dos puntas izq (arriba/abajo)
    //   Ojo der ("<"):  vértice izquierdo al medio, dos puntas der (arriba/abajo)
    int16_t hw = w / 2;
    int16_t hh = h / 2;

    int16_t x0, y0,  // punta izquierda superior
            x1, y1,  // punta izquierda inferior
            x2, y2;  // vértice central (pico)

    if (isLeft) {
        // ">" : pico a la derecha
        x0 = cx - hw;     y0 = cy - hh;
        x1 = cx - hw;     y1 = cy + hh;
        x2 = cx + hw - 1; y2 = cy;
    } else {
        // "<" : pico a la izquierda
        x0 = cx + hw - 1; y0 = cy - hh;
        x1 = cx + hw - 1; y1 = cy + hh;
        x2 = cx - hw;     y2 = cy;
    }

    u8.setDrawColor(1);
    // Triángulo principal
    u8.drawTriangle(x0, y0, x1, y1, x2, y2);

    // Borde extra para grosor visual
    u8.drawTriangle(x0-1, y0, x1-1, y1, x2, y2);
    u8.drawTriangle(x0+1, y0, x1+1, y1, x2, y2);
}

// ----------------------------------------------------------------
//  drawEyeCirculo — disco lleno (AMOR)
// ----------------------------------------------------------------
static void drawEyeCirculo(U8G2 &u8, int16_t cx, int16_t cy, int16_t w)
{
    int16_t rx = w / 2;
    if (rx < 3) rx = 3;
    u8.setDrawColor(1);
    u8.drawDisc((u8g2_uint_t)cx, (u8g2_uint_t)cy, (u8g2_uint_t)rx);
}

// ----------------------------------------------------------------
//  drawEyeCirculoPupila — disco blanco con puntito negro (SORPRENDIDO)
// ----------------------------------------------------------------
static void drawEyeCirculoPupila(U8G2 &u8, int16_t cx, int16_t cy, int16_t w)
{
    int16_t rx = w / 2;
    if (rx < 4) rx = 4;
    u8.setDrawColor(1);
    u8.drawDisc((u8g2_uint_t)cx, (u8g2_uint_t)cy, (u8g2_uint_t)rx);
    // Pupila: disco negro de 3px en el centro
    u8.setDrawColor(0);
    u8.drawDisc((u8g2_uint_t)cx, (u8g2_uint_t)cy, 3);
    u8.setDrawColor(1);
}

// ----------------------------------------------------------------
//  drawEyeSlab — barra rectangular inclinada (SOSPECHOSO ojo izq)
//
//  El ojo izquierdo del sospechoso es una barra gruesa diagonal:
//  más alta a la derecha (esquina interna) y más baja a la izquierda.
//  Se implementa como drawBox + recorte/pendiente con un drawTriangle negro.
// ----------------------------------------------------------------
static void drawEyeSlab(U8G2 &u8, int16_t cx, int16_t cy, int16_t w)
{
    // La barra mide w píxeles de ancho y ~6px de alto
    int16_t hw  = w / 2;
    int16_t barH = 5;   // altura de la barra

    // Coordenadas base centradas
    int16_t x0  = cx - hw;
    int16_t x1  = cx + hw;

    // Pendiente: esquina derecha (interna) 4px más arriba
    int16_t slope = 4;
    int16_t yL_top = cy - barH / 2 + slope;   // borde superior izq (más bajo)
    int16_t yL_bot = yL_top + barH;
    int16_t yR_top = cy - barH / 2;            // borde superior der (más alto)
    int16_t yR_bot = yR_top + barH;

    // Dibujar como dos triángulos que forman un paralelogramo
    u8.setDrawColor(1);
    // Triángulo superior
    u8.drawTriangle(x0, yL_top, x1, yR_top, x1, yR_bot);
    // Triángulo inferior
    u8.drawTriangle(x0, yL_top, x0, yL_bot, x1, yR_bot);

    u8.setDrawColor(1);
}

// ================================================================
//  Face::drawEye  —  despachador por EyeStyle
// ================================================================
void Face::drawEye(U8G2 &u8, const EyeParams &p)
{
    // Determinar qué estilo corresponde a este ojo
    // (izquierdo si cx < 64, derecho si cx >= 64)
    bool isLeft   = (p.cx < 64.0f);
    EyeStyle style = isLeft ? s_leftStyle : s_rightStyle;

    // Centro con offset de mirada/respiración
    int16_t cx = (int16_t)(p.cx + p.offX);
    int16_t cy = (int16_t)(p.cy + p.offY);
    int16_t w  = (int16_t)clampf(p.w, 4.0f, 60.0f);
    int16_t h  = (int16_t)clampf(p.h, 2.0f, 40.0f);

    switch (style) {

    // ---- RECT: comportamiento original con párpados ----
    case EyeStyle::RECT:
    {
        float wf    = clampf(p.w, 4.0f, 60.0f);
        float hf    = clampf(p.h, 2.0f, 40.0f);
        float r     = clampf(p.r, 0.0f, (wf < hf ? wf : hf) * 0.5f);
        float pTop  = clampf(p.pTop, 0.0f, hf);
        float pBot  = clampf(p.pBot, 0.0f, hf - pTop);

        float eyeX = p.cx + p.offX - wf * 0.5f;
        float eyeY = p.cy + p.offY - hf * 0.5f;

        int16_t ix  = (int16_t)eyeX;
        int16_t iy  = (int16_t)eyeY;
        int16_t iw  = (int16_t)wf;
        int16_t ih  = (int16_t)hf;
        int16_t ir  = (int16_t)r;

        if (iw < 1) iw = 1;
        if (ih < 1) ih = 1;
        if (ir < 0) ir = 0;
        int16_t rmax = (iw < ih ? iw : ih) / 2;
        if (ir > rmax) ir = rmax;

        // 1. Cuerpo del ojo blanco
        u8.setDrawColor(1);
        u8.drawRBox((u8g2_uint_t)ix, (u8g2_uint_t)iy,
                    (u8g2_uint_t)iw, (u8g2_uint_t)ih,
                    (u8g2_uint_t)ir);

        // 2. Párpado superior
        if (pTop > 0.5f) {
            u8.setDrawColor(0);
            int16_t ptop = (int16_t)pTop;
            int16_t yEyeTop  = iy;
            int16_t yLidBase = yEyeTop + ptop;
            int16_t yLidL    = yLidBase;
            int16_t yLidR    = yLidBase - (int16_t)p.slopeTop;
            int16_t yLidMin  = yLidL < yLidR ? yLidL : yLidR;
            int16_t rectH    = yLidMin - yEyeTop;
            if (rectH > 0) {
                u8.drawBox((u8g2_uint_t)ix, (u8g2_uint_t)yEyeTop,
                           (u8g2_uint_t)iw, (u8g2_uint_t)rectH);
            }
            if ((int16_t)p.slopeTop != 0) {
                int16_t tcx, tcy;
                if (p.slopeTop > 0) {
                    tcx = ix + iw - 1; tcy = yLidR;
                } else {
                    tcx = ix;          tcy = yLidL;
                }
                u8.drawTriangle(ix, yLidMin,
                                ix + iw - 1, yLidMin,
                                tcx, tcy);
            }
        }

        // 3. Párpado inferior
        if (pBot > 0.5f) {
            u8.setDrawColor(0);
            int16_t pbot    = (int16_t)pBot;
            int16_t yEyeBot = iy + ih;
            int16_t yLidTop = yEyeBot - pbot;
            int16_t yLidL   = yLidTop;
            int16_t yLidR   = yLidTop - (int16_t)p.slopeBot;
            int16_t yLidMin = yLidL < yLidR ? yLidL : yLidR;
            int16_t rectH   = yEyeBot - yLidMin;
            if (rectH > 0) {
                u8.drawBox((u8g2_uint_t)ix, (u8g2_uint_t)yLidMin,
                           (u8g2_uint_t)iw, (u8g2_uint_t)rectH);
            }
            if ((int16_t)p.slopeBot != 0) {
                int16_t tcx, tcy;
                if (p.slopeBot > 0) {
                    tcx = ix + iw - 1; tcy = yLidR;
                } else {
                    tcx = ix;          tcy = yLidL;
                }
                u8.drawTriangle(ix, yLidMin,
                                ix + iw - 1, yLidMin,
                                tcx, tcy);
            }
        }

        u8.setDrawColor(1);
        break;
    }

    case EyeStyle::ARCO_ARRIBA:
        drawEyeArcoArriba(u8, cx, cy, w);
        break;

    case EyeStyle::ARCO_ABAJO:
        drawEyeArcoAbajo(u8, cx, cy, w);
        break;

    case EyeStyle::ANGULO:
        drawEyeAngulo(u8, cx, cy, w, h, isLeft);
        break;

    case EyeStyle::CIRCULO:
        drawEyeCirculo(u8, cx, cy, w);
        break;

    case EyeStyle::CIRCULO_PUPILA:
        drawEyeCirculoPupila(u8, cx, cy, w);
        break;

    case EyeStyle::SLAB:
        drawEyeSlab(u8, cx, cy, w);
        break;

    case EyeStyle::CRUZ:
        // Reservado — dibuja X con líneas gruesas
        {
            int16_t hw2 = w / 2 - 2;
            int16_t hh2 = h / 2 - 2;
            u8.setDrawColor(1);
            for (int16_t t = -2; t <= 2; t++) {
                u8.drawLine(cx - hw2, cy - hh2 + t, cx + hw2, cy + hh2 + t);
                u8.drawLine(cx + hw2, cy - hh2 + t, cx - hw2, cy + hh2 + t);
            }
        }
        break;
    }
}

// ----------------------------------------------------------------
//  Dibuja corazón pequeño flotando arriba del centro (AMOR)
//  Dos discos + un triángulo invertido
// ----------------------------------------------------------------
static void drawHeartSmall(U8G2 &u8, int16_t cx, int16_t cy)
{
    // Radio de los discos superiores
    int16_t r = 4;
    // Dos discos que forman la parte superior del corazón
    u8.setDrawColor(1);
    u8.drawDisc((u8g2_uint_t)(cx - r + 1), (u8g2_uint_t)cy,       (u8g2_uint_t)r);
    u8.drawDisc((u8g2_uint_t)(cx + r - 1), (u8g2_uint_t)cy,       (u8g2_uint_t)r);
    // Triángulo invertido que forma la punta inferior del corazón
    u8.drawTriangle(cx - r * 2 + 1, cy + 1,
                    cx + r * 2 - 1, cy + 1,
                    cx,             cy + r * 2);
}

// ----------------------------------------------------------------
//  Face::render  —  dibuja ambos ojos en el buffer (sin clear/send)
// ----------------------------------------------------------------
void Face::render(U8G2 &u8)
{
    drawEye(u8, _leftCur);
    drawEye(u8, _rightCur);

    // Texto "Zzz" para DORMIDO
    if (_expr == Expression::DORMIDO) {
        u8.setDrawColor(1);
        u8.setFont(u8g2_font_4x6_tf);
        u8.drawStr(100, 12, "Zzz");
    }

    // Corazón flotando arriba para AMOR
    if (_expr == Expression::AMOR) {
        drawHeartSmall(u8, 64, 8);
    }
}
