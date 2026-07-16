// =============================================================
//  face.cpp — Motor de cara y expresiones para espToy
//  Plataforma : Seeed XIAO ESP32-S3
//  Display    : OLED SSD1309 128×64, librería U8g2
//
//  v2: cada expresión tiene un EyeStyle por ojo.
//  v3 (doc 06 §2): fases INTRO → LOOP → OUTRO por expresión,
//  moduladores de loop (rebote, pulso, temblor, barrido) y
//  sistema de partículas para los extras (corazones, Zzz, lágrima).
// =============================================================

#include "face.h"
#include "config.h"   // EASING_*, PARPADEO_*, MIRADA_*, ANIM_*
#include <math.h>     // sinf, cosf, fabsf

// ----------------------------------------------------------------
//  Instancia global
// ----------------------------------------------------------------
Face face;

// ----------------------------------------------------------------
//  Tipos de partícula
// ----------------------------------------------------------------
static const uint8_t PART_LIBRE   = 0;
static const uint8_t PART_CORAZON = 1;
static const uint8_t PART_ZZZ     = 2;
static const uint8_t PART_LAGRIMA = 3;

// ----------------------------------------------------------------
//  EyeStyle: tipo de dibujo para cada ojo
// ----------------------------------------------------------------
enum class EyeStyle : uint8_t {
    RECT,            // rectángulo redondeado con párpados (comportamiento original)
    ARCO_ARRIBA,     // "^^"  — semicírculo con abertura hacia abajo (sin uso en tabla; funciones conservadas)
    ARCO_ABAJO,      // "‿‿"  — semicírculo con abertura hacia arriba (DORMIDO, mitad inferior)
    ANGULO,          // "> <" — triángulo/ángulo apuntando al centro (ENOJADO)
    CIRCULO,         // disco sólido sin pupila (AMOR, SORPRENDIDO)
    CIRCULO_PUPILA,  // disco blanco con puntito negro central (SORPRENDIDO diferenciado)
    SLAB,            // barra rectangular delgada e inclinada (SOSPECHOSO — ojo entrecerrado)
    CRUZ,            // X — dos líneas diagonales gruesas (reservado / error)
    ESPIRAL,         // remolino "@" — espiral giratoria animada (MAREADO)
    CIRCULO_BRILLO,  // disco grande con estrella/destello blanco interno (ILUSIONADO — ojos brillantes)
    MEDIALUNA,       // ojo recortado en su parte inferior por un arco negro (FELIZ — sonrisa en los ojos)
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
//         ABURRIDO, DORMIDO, SOSPECHOSO, AMOR, GUINO, RISA, MAREADO,
//         ILUSIONADO
//
//  Los campos pTop/pBot/slope sólo tienen efecto en EyeStyle::RECT.
// ----------------------------------------------------------------
static const ExprDef EXPR_TABLE[13] = {

    // 0 — NEUTRAL: rectángulos redondeados normales
    { 0, 0, 0, 0, EyeStyle::RECT,
      0, 0, 0, 0, EyeStyle::RECT,
      28, 22, 6 },

    // 1 — FELIZ: ojos en medialuna — disco blanco con la parte inferior tapada por un arco
    //   negro desplazado hacia abajo, dejando visible solo la mitad superior del ojo.
    //   El resultado es una medialuna que mira hacia arriba (mejillas de sonrisa).
    //   w=28 h=22 r=11 mantienen el tamaño anterior; MEDIALUNA ignora pTop/pBot.
    { 0, 0, 0, 0, EyeStyle::MEDIALUNA,
      0, 0, 0, 0, EyeStyle::MEDIALUNA,
      28, 22, 11 },

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
    //   (diseño original; al usuario le gusta así)
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

    // 9 — GUINO: ojo izq abierto normal (rect), ojo der cerrado (línea fina con rPTop=18)
    { 0, 0, 0, 0, EyeStyle::RECT,
      18, 0, 0, 0, EyeStyle::RECT,
      28, 22, 6 },

    // 10 — RISA (cosquillas): ojos apretados de carcajada "^  ^" (arco hacia arriba),
    //   el estilo clásico de reírse fuerte. Sin boca: la risa se lee por los ojos
    //   cerrados en arco + el rebote vertical enérgico del loop.
    //   Distinto de FELIZ (medialuna llena y curva): aquí los ojos son arcos finos apretados.
    { 0, 0, 0, 0, EyeStyle::ARCO_ARRIBA,
      0, 0, 0, 0, EyeStyle::ARCO_ARRIBA,
      28, 16, 0 },

    // 11 — MAREADO: ojos en espiral giratoria "@_@" — efecto mareo clásico.
    //   El dibujo de la espiral se hace en drawEyeEspiral() animada por _loopPhase.
    //   w=26 h=26 r=13 define el área del ojo; el estilo ESPIRAL ignora pTop/pBot.
    { 0, 0, 0, 0, EyeStyle::ESPIRAL,
      0, 0, 0, 0, EyeStyle::ESPIRAL,
      26, 26, 13 },

    // 12 — ILUSIONADO: ojos grandes y brillantes mirando levemente arriba.
    //   Discos grandes con highlight blanco interno desplazado arriba-izquierda
    //   que da la sensación clásica de "ojos que brillan de emoción/ilusión".
    //   Distinto de SORPRENDIDO (CIRCULO_PUPILA con pupila negra animada) porque
    //   aquí el highlight es BLANCO y la mirada mira arriba (offY negativo fijo
    //   aportado por _animOffY en el LOOP); distinto de AMOR (disco sólido sin brillo).
    //   w=30 h=30 r=15: ojos más grandes que NEUTRAL para enfatizar la ilusión.
    { 0, 0, 0, 0, EyeStyle::CIRCULO_BRILLO,
      0, 0, 0, 0, EyeStyle::CIRCULO_BRILLO,
      30, 30, 15 },
};

// ----------------------------------------------------------------
//  EyeStylePair: estilos activos para los dos ojos
//  Se actualiza instantáneamente al cargar la expresión.
// ----------------------------------------------------------------
static EyeStyle s_leftStyle  = EyeStyle::RECT;
static EyeStyle s_rightStyle = EyeStyle::RECT;

// Radio de pupila para SORPRENDIDO (lo anima el loop: pulsa 2..4 px)
static int16_t s_pupilaR = 3;

// Fase de rotación de la espiral MAREADO (avanza cada frame en updateLoop)
static float s_espiralFase = 0.0f;

// Radio del highlight de ILUSIONADO (oscila entre twinkle mín/máx en el loop)
static float s_brilloR = ANIM_ILUSIONADO_HIGHLIGHT_R;

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
    if (idx >= 13) idx = 0;
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
//  Schedulers de gestos idle
// ----------------------------------------------------------------
void Face::scheduleNextBostezo(uint32_t now)
{
    uint32_t rango = GESTO_BOSTEZO_MAX_MS - GESTO_BOSTEZO_MIN_MS;
    _sigBostezo = now + GESTO_BOSTEZO_MIN_MS + (uint32_t)(random((long)rango));
}

void Face::scheduleNextSacudida(uint32_t now)
{
    uint32_t rango = GESTO_SACUDIDA_MAX_MS - GESTO_SACUDIDA_MIN_MS;
    _sigSacudida = now + GESTO_SACUDIDA_MIN_MS + (uint32_t)(random((long)rango));
}

void Face::scheduleNextMiradaFija(uint32_t now)
{
    uint32_t rango = GESTO_MIRADA_MAX_MS - GESTO_MIRADA_MIN_MS;
    _sigMiradaFija = now + GESTO_MIRADA_MIN_MS + (uint32_t)(random((long)rango));
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

    _fase         = AnimFase::LOOP;
    _faseInicioMs = 0;
    _hayPendiente = false;
    _pendiente    = Expression::NEUTRAL;
    _animOffX = _animOffY = 0.0f;
    _animEscala  = 1.0f;
    _animEscalaY = 1.0f;
    _lidExtra    = 0.0f;
    _loopPhase   = 0.0f;
    _sigTemblorMs = 0;
    _temblorX     = 0.0f;
    _sigSpawnMs   = 0;
    _spawnLado    = false;
    _lastNow      = 0;
    limpiarParticulas();

    // Gestos idle
    _gesto          = GestoIdle::NINGUNO;
    _gestoInicioMs  = 0;

    uint32_t now = millis();
    scheduleNextBlink(now);
    scheduleNextGaze(now);
    scheduleNextBostezo(now);
    scheduleNextSacudida(now);
    scheduleNextMiradaFija(now);
}

// ----------------------------------------------------------------
//  Face::setExpression — dispara la transición animada
//  OUTRO (squash de la actual) → carga la nueva → INTRO (pop)
// ----------------------------------------------------------------
void Face::setExpression(Expression e)
{
    if (_fase == AnimFase::OUTRO) {
        // Ya está saliendo: solo actualizar el destino
        _pendiente = e;
        _hayPendiente = true;
        return;
    }
    if (e == _expr) return;

    _pendiente    = e;
    _hayPendiente = true;
    _fase         = AnimFase::OUTRO;
    _faseInicioMs = millis();
}

// ----------------------------------------------------------------
//  Face::cambiarExpresion — carga la nueva expresión y arranca INTRO
// ----------------------------------------------------------------
void Face::cambiarExpresion(Expression e, uint32_t now)
{
    _expr = e;
    exprToTargets(e, _leftTgt, _rightTgt);
    _fase         = AnimFase::INTRO;
    _faseInicioMs = now;
    _loopPhase    = 0.0f;
    _lidExtra     = 0.0f;
    _animOffX = _animOffY = 0.0f;
    _sigSpawnMs   = now;          // el primer extra sale enseguida
    s_espiralFase = 0.0f;        // reiniciar fase de espiral al entrar a cualquier expresión
    s_brilloR     = ANIM_ILUSIONADO_HIGHLIGHT_R;  // reiniciar brillo de ILUSIONADO
    limpiarParticulas();

    // Cancelar gesto activo al cambiar de expresión
    // (los timers de próximo disparo siguen corriendo)
    _gesto = GestoIdle::NINGUNO;
}

// ----------------------------------------------------------------
//  Face::expression
// ----------------------------------------------------------------
Expression Face::expression() const
{
    return _expr;
}

// ----------------------------------------------------------------
//  Partículas
// ----------------------------------------------------------------
void Face::limpiarParticulas()
{
    for (uint8_t i = 0; i < ANIM_PARTICULAS_MAX; i++) _partes[i].tipo = PART_LIBRE;
}

void Face::spawnParticula(uint8_t tipo, float x, float y,
                          float vx, float vy, uint16_t vidaMs, uint32_t now)
{
    for (uint8_t i = 0; i < ANIM_PARTICULAS_MAX; i++) {
        if (_partes[i].tipo != PART_LIBRE) continue;
        _partes[i] = { tipo, x, y, vx, vy, now, vidaMs };
        return;
    }
    // sin slots libres: se omite (no es crítico)
}

void Face::updateParticulas(uint32_t now)
{
    for (uint8_t i = 0; i < ANIM_PARTICULAS_MAX; i++) {
        Particula &p = _partes[i];
        if (p.tipo == PART_LIBRE) continue;

        uint32_t edad = now - p.nacioMs;
        if (edad >= p.vidaMs || p.y < -8.0f || p.y > 70.0f || p.x > 132.0f) {
            p.tipo = PART_LIBRE;
            continue;
        }

        switch (p.tipo) {
        case PART_CORAZON:
            // Flota hacia arriba con deriva senoidal
            p.y += p.vy;
            p.x += sinf((float)edad * 0.012f) * 0.5f;
            break;

        case PART_ZZZ:
            // Sube en diagonal derecha
            p.x += p.vx;
            p.y += p.vy;
            break;

        case PART_LAGRIMA:
            // Primero crece quieta en el borde del ojo, luego cae con gravedad
            if (edad > 600) {
                p.vy += 0.18f;
                p.y  += p.vy;
            }
            break;
        }
    }
}

// ----------------------------------------------------------------
//  Face::updateLoop — moduladores por expresión (INTRO y LOOP)
// ----------------------------------------------------------------
void Face::updateLoop(uint32_t now)
{
    _animOffX = 0.0f;
    _animOffY = 0.0f;
    _lidExtra = 0.0f;
    float pulso = 1.0f;

    switch (_expr) {

    case Expression::FELIZ:
        // Rebote vertical alegre (solo hacia arriba, como saltitos)
        _loopPhase += ANIM_FELIZ_VEL;
        _animOffY = -fabsf(sinf(_loopPhase)) * ANIM_FELIZ_AMPL_PX;
        break;

    case Expression::AMOR:
        // Ojos pulsando tamaño + corazones flotando
        _loopPhase += ANIM_AMOR_VEL;
        pulso = 1.0f + ANIM_AMOR_PULSO * sinf(_loopPhase);
        if ((int32_t)(now - _sigSpawnMs) >= 0) {
            _spawnLado = !_spawnLado;
            float px = (_spawnLado ? EYE_LEFT_CX : EYE_RIGHT_CX)
                       + (float)(random(9)) - 4.0f;
            spawnParticula(PART_CORAZON, px, 16.0f, 0.0f, -0.55f,
                           ANIM_CORAZON_VIDA_MS, now);
            _sigSpawnMs = now + ANIM_CORAZON_SPAWN_MS;
        }
        break;

    case Expression::DORMIDO:
        // Z's subiendo en diagonal (la respiración lenta se maneja aparte)
        if ((int32_t)(now - _sigSpawnMs) >= 0) {
            spawnParticula(PART_ZZZ, 96.0f, 26.0f, 0.38f, -0.5f,
                           ANIM_ZZZ_VIDA_MS, now);
            _sigSpawnMs = now + ANIM_ZZZ_SPAWN_MS;
        }
        break;

    case Expression::ENOJADO:
        // Temblor horizontal aleatorio
        if ((int32_t)(now - _sigTemblorMs) >= 0) {
            _temblorX    = (float)(random(3)) - 1.0f;   // -1, 0, +1
            _sigTemblorMs = now + ANIM_ENOJADO_TEMBLOR_MS;
        }
        _animOffX = _temblorX;
        break;

    case Expression::TRISTE:
        // Micro-temblor de párpados + lágrima periódica
        _loopPhase += 0.3f;
        _animOffY = sinf(_loopPhase) * 0.4f;
        if ((int32_t)(now - _sigSpawnMs) >= 0) {
            spawnParticula(PART_LAGRIMA, EYE_LEFT_CX - 10.0f, 44.0f,
                           0.0f, 0.0f, ANIM_LAGRIMA_VIDA_MS, now);
            _sigSpawnMs = now + ANIM_LAGRIMA_SPAWN_MS;
        }
        break;

    case Expression::ABURRIDO: {
        // Párpado superior baja lentamente hasta casi cerrar y reabre
        float ph = (float)(now % ANIM_ABURRIDO_CICLO_MS) / (float)ANIM_ABURRIDO_CICLO_MS;
        _lidExtra = (0.5f - 0.5f * cosf(ph * 6.2832f)) * (_leftTgt.h * 0.55f);
        break;
    }

    case Expression::SORPRENDIDO:
        // Pupila pulsando
        _loopPhase += 0.12f;
        s_pupilaR = (int16_t)(3.0f + sinf(_loopPhase) * 1.4f);
        if (s_pupilaR < 2) s_pupilaR = 2;
        break;

    case Expression::SOSPECHOSO:
        // La mirada barre lentamente izquierda ↔ derecha
        _gazeTgtX = sinf((float)now * ANIM_SOSP_VEL) * ANIM_SOSP_RANGO_PX;
        _gazeTgtY = 0.0f;
        break;

    case Expression::RISA:
        // Rebote vertical enérgico — igual que FELIZ pero más amplio y rápido
        _loopPhase += ANIM_RISA_VEL;
        _animOffY = -fabsf(sinf(_loopPhase)) * ANIM_RISA_AMPL_PX;
        break;

    case Expression::MAREADO:
        // Rotación de la espiral: avanza la fase para dar el efecto de giro continuo
        s_espiralFase += ANIM_MAREADO_VEL_ROT;
        if (s_espiralFase > 6.2832f) s_espiralFase -= 6.2832f;
        // Micro-oscilación vertical suave (tambaleo de mareo)
        _loopPhase += 0.05f;
        _animOffY = sinf(_loopPhase) * 1.5f;
        break;

    case Expression::ILUSIONADO:
        // Twinkle: pulso sutil de escala + mirada hacia arriba fija
        // El highlight también oscila de tamaño (s_brilloR) para dar el efecto vivo.
        _loopPhase += ANIM_ILUSIONADO_VEL;
        pulso = 1.0f + ANIM_ILUSIONADO_PULSO * sinf(_loopPhase);
        s_brilloR = ANIM_ILUSIONADO_HIGHLIGHT_R
                    + 1.0f * sinf(_loopPhase * 1.3f);  // oscila ~1.5..3.5 px
        if (s_brilloR < 1.0f) s_brilloR = 1.0f;
        // Mirada fija levemente hacia arriba para la sensación de "alzado"
        _animOffY = -3.0f;
        break;

    default:
        break;   // NEUTRAL, GUINO: solo capa base
    }

    // Escala de fase (overshoot INTRO / squash OUTRO) sobre el pulso del loop
    _animEscala  = pulso;
    _animEscalaY = 1.0f;

    if (_fase == AnimFase::INTRO) {
        float t = (float)(now - _faseInicioMs) / (float)ANIM_INTRO_MS;
        t = clampf(t, 0.0f, 1.0f);
        float os = (_expr == Expression::SORPRENDIDO) ? ANIM_OVERSHOOT_SORPRESA
                                                      : ANIM_OVERSHOOT;
        // Pop: crece por encima de 1 y asienta (senoidal medio ciclo)
        _animEscala *= 1.0f + os * sinf(t * 3.1416f);
    } else if (_fase == AnimFase::OUTRO) {
        float t = (float)(now - _faseInicioMs) / (float)ANIM_OUTRO_MS;
        t = clampf(t, 0.0f, 1.0f);
        // Squash: la altura se comprime hacia ANIM_SQUASH_MIN
        _animEscalaY = 1.0f - (1.0f - ANIM_SQUASH_MIN) * t;
    }
}

// ----------------------------------------------------------------
//  Face::updateGestos — gestos idle (se llama desde update() tras updateLoop)
//
//  Disparo: solo si fase == LOOP, sin gesto activo, sin parpadeo en curso
//  y la expresión es la permitida para ese gesto.
//  Un solo gesto a la vez; al terminar se reprograma el timer.
// ----------------------------------------------------------------
void Face::updateGestos(uint32_t now)
{
    // ---- Disparo de nuevo gesto (solo si no hay ninguno activo) ----
    if (_gesto == GestoIdle::NINGUNO &&
        _fase   == AnimFase::LOOP    &&
        _blinkState == BlinkState::IDLE)
    {
        // Bostezo: NEUTRAL o ABURRIDO
        if ((int32_t)(now - _sigBostezo) >= 0 &&
            (_expr == Expression::NEUTRAL || _expr == Expression::ABURRIDO))
        {
            _gesto         = GestoIdle::BOSTEZO;
            _gestoInicioMs = now;
            Serial.println("[face] gesto: bostezo");
        }
        // Sacudida: solo NEUTRAL
        else if ((int32_t)(now - _sigSacudida) >= 0 &&
                 _expr == Expression::NEUTRAL)
        {
            _gesto         = GestoIdle::SACUDIDA;
            _gestoInicioMs = now;
            Serial.println("[face] gesto: sacudida");
        }
        // Mirada fija: solo NEUTRAL
        else if ((int32_t)(now - _sigMiradaFija) >= 0 &&
                 _expr == Expression::NEUTRAL)
        {
            _gesto         = GestoIdle::MIRADA_FIJA;
            _gestoInicioMs = now;
            _gazeTgtX      = 0.0f;   // fuerza mirada al centro
            _gazeTgtY      = 0.0f;
            Serial.println("[face] gesto: mirada_fija");
        }
    }

    // ---- Ejecución del gesto activo --------------------------------
    if (_gesto == GestoIdle::NINGUNO) return;

    uint32_t t = now - _gestoInicioMs;   // tiempo desde el inicio del gesto

    switch (_gesto) {

    // ---- BOSTEZO ------------------------------------------------
    case GestoIdle::BOSTEZO: {
        if (t < BOSTEZO_T_AGRANDA_MS) {
            // Tramo 1 (0–400 ms): ojos se agrandan gradualmente hasta ×1.25
            float progreso = (float)t / (float)BOSTEZO_T_AGRANDA_MS;
            _animEscala = 1.0f + (BOSTEZO_ESCALA_MAX - 1.0f) * progreso;
            _lidExtra   = 0.0f;

        } else if (t < BOSTEZO_T_CIERRA_MS) {
            // Tramo 2 (400–1200 ms): cierre lento + escala vuelve a 1.0
            float dur     = (float)(BOSTEZO_T_CIERRA_MS - BOSTEZO_T_AGRANDA_MS);
            float progreso = (float)(t - BOSTEZO_T_AGRANDA_MS) / dur;
            _animEscala = BOSTEZO_ESCALA_MAX - (BOSTEZO_ESCALA_MAX - 1.0f) * progreso;
            _lidExtra   = progreso * _leftTgt.h;   // párpado sube hasta tapar el ojo

        } else if (t < BOSTEZO_T_CERRADO_MS) {
            // Tramo 3 (1200–1500 ms): ojos cerrados quietos
            _animEscala = 1.0f;
            _lidExtra   = _leftTgt.h;

        } else if (t < BOSTEZO_T_TOTAL_MS) {
            // Tramo 4 (1500–1900 ms): reabre gradualmente
            float dur      = (float)(BOSTEZO_T_TOTAL_MS - BOSTEZO_T_CERRADO_MS);
            float progreso = (float)(t - BOSTEZO_T_CERRADO_MS) / dur;
            _animEscala = 1.0f;
            _lidExtra   = _leftTgt.h * (1.0f - progreso);

        } else {
            // Gesto terminado
            _animEscala = 1.0f;
            _lidExtra   = 0.0f;
            _gesto      = GestoIdle::NINGUNO;
            scheduleNextBostezo(now);
        }
        break;
    }

    // ---- SACUDIDA -----------------------------------------------
    case GestoIdle::SACUDIDA: {
        if (t < SACUDIDA_DURACION_MS) {
            // Alterna ±amplitud cada frame (efecto de sacudida rápida)
            _animOffX = ((t / 33) % 2 == 0) ? SACUDIDA_AMPLITUD_PX
                                              : -SACUDIDA_AMPLITUD_PX;
        } else {
            _animOffX = 0.0f;
            _gesto    = GestoIdle::NINGUNO;
            scheduleNextSacudida(now);
        }
        break;
    }

    // ---- MIRADA_FIJA --------------------------------------------
    case GestoIdle::MIRADA_FIJA: {
        if (t < MIRADA_FIJA_DURACION_MS) {
            // Mantiene mirada al centro y párpados levemente caídos
            _gazeTgtX = 0.0f;
            _gazeTgtY = 0.0f;
            _lidExtra = _leftTgt.h * MIRADA_FIJA_LID_FRAC;
        } else {
            _lidExtra = 0.0f;
            _gesto    = GestoIdle::NINGUNO;
            scheduleNextMiradaFija(now);
        }
        break;
    }

    default:
        break;
    }
}

// ----------------------------------------------------------------
//  Face::update  —  llamar una vez por frame (now = millis())
// ----------------------------------------------------------------
void Face::update(uint32_t now)
{
    _lastNow = now;

    // --- 0. Avance de fase INTRO/LOOP/OUTRO ----------------------
    if (_fase == AnimFase::OUTRO) {
        if ((now - _faseInicioMs) >= ANIM_OUTRO_MS) {
            cambiarExpresion(_hayPendiente ? _pendiente : _expr, now);
            _hayPendiente = false;
        }
    } else if (_fase == AnimFase::INTRO) {
        if ((now - _faseInicioMs) >= ANIM_INTRO_MS) {
            _fase = AnimFase::LOOP;
        }
    }

    // --- 1. Interpolación expresión (ease-out) -------------------
    lerpEye(_leftCur,  _leftTgt,  EASING_EXPRESION);
    lerpEye(_rightCur, _rightTgt, EASING_EXPRESION);

    // --- 2. Mirada errante --------------------------------------
    // (SOSPECHOSO pisa el objetivo con su barrido en updateLoop)
    if (now >= _gazeNextMs) {
        float rango = MIRADA_RANGO_PX * 2.0f;
        _gazeTgtX = -MIRADA_RANGO_PX + (float)(random((long)(rango * 100))) / 100.0f;
        _gazeTgtY = -MIRADA_RANGO_PX + (float)(random((long)(rango * 100))) / 100.0f;
        scheduleNextGaze(now);
    }

    // --- 3. Moduladores del loop por expresión -------------------
    updateLoop(now);

    // --- 3b. Gestos idle (pisan moduladores del loop cuando aplica) ---
    updateGestos(now);

    _gazeOffX += (_gazeTgtX - _gazeOffX) * EASING_MIRADA;
    _gazeOffY += (_gazeTgtY - _gazeOffY) * EASING_MIRADA;

    _leftCur.offX  = _gazeOffX;
    _leftCur.offY  = _gazeOffY;
    _rightCur.offX = _gazeOffX;
    _rightCur.offY = _gazeOffY;

    // --- 4. Micro-movimiento senoidal (respiración) -------------
    // DORMIDO respira más lento y más amplio (doc 06 §2.2)
    float breathOff;
    if (_expr == Expression::DORMIDO) {
        _breathPhase += ANIM_DORMIDO_VEL;
        breathOff = sinf(_breathPhase) * ANIM_DORMIDO_AMPL_PX;
    } else {
        _breathPhase += 0.063f;
        breathOff = sinf(_breathPhase) * 1.0f;
    }
    if (_breathPhase > 6.2832f) _breathPhase -= 6.2832f;
    _leftCur.offY  += breathOff;
    _rightCur.offY += breathOff;

    // --- 5. Partículas -------------------------------------------
    updateParticulas(now);

    // --- 6. Parpadeo --------------------------------------------
    // Solo para expresiones con ojos "abiertos" (RECT, CIRCULO*)
    // Los estilos cerrados (ARCO_ABAJO, SLAB, ANGULO) no parpadean.
    bool estiloIzqParpadeaOk = (s_leftStyle  == EyeStyle::RECT ||
                                 s_leftStyle  == EyeStyle::CIRCULO ||
                                 s_leftStyle  == EyeStyle::CIRCULO_PUPILA ||
                                 s_leftStyle  == EyeStyle::CIRCULO_BRILLO ||
                                 s_leftStyle  == EyeStyle::MEDIALUNA);
    bool estiloDerParpadeaOk = (s_rightStyle == EyeStyle::RECT ||
                                 s_rightStyle == EyeStyle::CIRCULO ||
                                 s_rightStyle == EyeStyle::CIRCULO_PUPILA ||
                                 s_rightStyle == EyeStyle::CIRCULO_BRILLO ||
                                 s_rightStyle == EyeStyle::MEDIALUNA);
    // ESPIRAL (MAREADO) no parpadea — el remolino debe verse continuo
    if (s_leftStyle  == EyeStyle::ESPIRAL) estiloIzqParpadeaOk = false;
    if (s_rightStyle == EyeStyle::ESPIRAL) estiloDerParpadeaOk = false;
    bool puedeParpardear = (estiloIzqParpadeaOk || estiloDerParpadeaOk);
    bool dormido = (_expr == Expression::DORMIDO);
    // Durante bostezo y mirada fija el parpadeo queda suspendido
    bool gestoSuspendeBlink = (_gesto == GestoIdle::BOSTEZO ||
                               _gesto == GestoIdle::MIRADA_FIJA);

    if (!dormido && puedeParpardear && !gestoSuspendeBlink) {
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
//  El radio de la pupila lo anima el loop (s_pupilaR, pulsa 2..4 px).
// ----------------------------------------------------------------
static void drawEyeCirculoPupila(U8G2 &u8, int16_t cx, int16_t cy, int16_t w)
{
    int16_t rx = w / 2;
    if (rx < 4) rx = 4;
    u8.setDrawColor(1);
    u8.drawDisc((u8g2_uint_t)cx, (u8g2_uint_t)cy, (u8g2_uint_t)rx);
    // Pupila: disco negro animado en el centro
    u8.setDrawColor(0);
    u8.drawDisc((u8g2_uint_t)cx, (u8g2_uint_t)cy, (u8g2_uint_t)s_pupilaR);
    u8.setDrawColor(1);
}

// ----------------------------------------------------------------
//  drawEyeCirculoBrillo — disco grande con estrella/destello (ILUSIONADO)
//
//  Técnica: disco blanco lleno (ojo grande abierto) + disco negro interior
//  (iris oscuro) + estrella de 4 puntas blanca en la esquina superior-izquierda.
//  El efecto es el ojo clásico de "ilusión/emoción" de los personajes anime.
//
//  La estrella se dibuja como dos cuñas (triángulos) cruzadas: una vertical
//  y una horizontal, formando una ✦ de 4 puntas. El "tamaño" de la estrella
//  (longitud de las puntas) escala con s_brilloR para el efecto twinkle.
//  ANIM_ILUSIONADO_HIGHLIGHT_OX/OY controlan la posición del destello.
// ----------------------------------------------------------------
static void drawEyeCirculoBrillo(U8G2 &u8, int16_t cx, int16_t cy, int16_t w)
{
    int16_t rx = w / 2;
    if (rx < 5) rx = 5;

    // Radio del iris oscuro: ojo grande con un hueco interno
    int16_t rIris = rx - 3;   // deja un borde blanco de ~3 px
    if (rIris < 3) rIris = 3;

    // 1. Disco blanco exterior (borde del ojo / esclerótica)
    u8.setDrawColor(1);
    u8.drawDisc((u8g2_uint_t)cx, (u8g2_uint_t)cy, (u8g2_uint_t)rx);

    // 2. Disco negro interior (iris oscuro)
    u8.setDrawColor(0);
    u8.drawDisc((u8g2_uint_t)cx, (u8g2_uint_t)cy, (u8g2_uint_t)rIris);

    // 3. Estrella de 4 puntas blanca desplazada arriba-izquierda (destello ✦)
    //    Longitud de las puntas: s_brilloR (oscila ~1.5..3.5 px con el twinkle).
    //    Se dibuja como dos triángulos cruzados que se afinan hacia las puntas,
    //    más un puntito central para reforzar el centro brillante.
    int16_t hx = cx + (int16_t)ANIM_ILUSIONADO_HIGHLIGHT_OX;
    int16_t hy = cy + (int16_t)ANIM_ILUSIONADO_HIGHLIGHT_OY;
    int16_t arm = (int16_t)s_brilloR;   // longitud de cada punta
    if (arm < 1) arm = 1;
    if (arm > rIris - 1) arm = rIris - 1;
    int16_t stub = (arm > 1) ? 1 : 0;  // medio ancho en la base de cada triángulo

    u8.setDrawColor(1);
    // Cuña vertical (punta arriba y punta abajo)
    u8.drawTriangle(hx,        hy - arm,   // punta superior
                    hx - stub, hy,          // base izq
                    hx + stub, hy);         // base der
    u8.drawTriangle(hx,        hy + arm,   // punta inferior
                    hx - stub, hy,
                    hx + stub, hy);
    // Cuña horizontal (punta izq y punta der)
    u8.drawTriangle(hx - arm,  hy,          // punta izquierda
                    hx,        hy - stub,   // base sup
                    hx,        hy + stub);  // base inf
    u8.drawTriangle(hx + arm,  hy,          // punta derecha
                    hx,        hy - stub,
                    hx,        hy + stub);
    // Puntito central para reforzar el centro
    u8.drawDisc((u8g2_uint_t)hx, (u8g2_uint_t)hy, 1);

    u8.setDrawColor(1);
}

// ----------------------------------------------------------------
//  drawEyeMedialuna — medialuna que abulta hacia arriba (FELIZ)
//
//  Técnica: disco blanco grande (ojo abierto) y luego se tapa la
//  mitad inferior con un disco negro ligeramente más grande desplazado
//  hacia abajo. El borde superior del disco negro recorta el ojo blanco
//  dejando visible solo la parte superior — una medialuna que mira arriba,
//  como si los cachetes subieran tapando la parte baja del ojo.
//
//  desplazamiento vertical del disco de corte: ~40 % del radio → medialuna
//  bien definida, ni ojo casi entero ni línea fina.
// ----------------------------------------------------------------
static void drawEyeMedialuna(U8G2 &u8, int16_t cx, int16_t cy, int16_t w)
{
    int16_t rx = w / 2;
    if (rx < 5) rx = 5;

    // 1. Disco blanco lleno (el ojo completo)
    u8.setDrawColor(1);
    u8.drawDisc((u8g2_uint_t)cx, (u8g2_uint_t)cy, (u8g2_uint_t)rx);

    // 2. Disco negro desplazado hacia abajo que recorta la mitad inferior.
    //    El radio del disco de corte es igual al del ojo (rx) para que el
    //    borde superior sea una curva suave y simétrica.
    //    Desplazamiento: ~67 % del radio → corta menos abajo, deja el ojo
    //    más lleno con una curva de sonrisa más suave.
    int16_t corteOff = (rx * 2) / 3;   // ~67 % del radio hacia abajo
    int16_t corteCy  = cy + corteOff;  // centro del disco de corte
    int16_t corteR   = rx + 1;         // radio del corte ligeramente mayor
    u8.setDrawColor(0);
    u8.drawDisc((u8g2_uint_t)cx, (u8g2_uint_t)corteCy, (u8g2_uint_t)corteR);

    u8.setDrawColor(1);
}

// ----------------------------------------------------------------
//  drawEyeEspiral — remolino "@" animado (MAREADO)
//
//  Dibuja una espiral aritmética: para t de 0 a Nvueltas·2π con paso
//  ANIM_MAREADO_PASO_RAD, el radio crece linealmente (r = k·t) y se
//  conectan puntos consecutivos con drawLine.  La fase de rotación
//  s_espiralFase se suma al ángulo para animar el giro continuo.
//
//  w  : ancho del ojo — limita el radio máximo a w/2 px.
// ----------------------------------------------------------------
static void drawEyeEspiral(U8G2 &u8, int16_t cx, int16_t cy, int16_t w)
{
    float rMax = (float)(w / 2);
    if (rMax < 4.0f) rMax = 4.0f;

    // Total de ángulo a recorrer
    float tMax = ANIM_MAREADO_VUELTAS * 6.2832f;
    // Factor k ajustado para que al final la espiral llegue a rMax
    float k = rMax / tMax;

    float paso = ANIM_MAREADO_PASO_RAD;

    // Punto inicial de la espiral (t=0 → r=0 → centro)
    int16_t px0 = cx;
    int16_t py0 = cy;

    u8.setDrawColor(1);

    for (float t = paso; t <= tMax + paso * 0.5f; t += paso) {
        float r = k * t;
        float ang = t + s_espiralFase;
        int16_t px1 = (int16_t)(cx + r * cosf(ang));
        int16_t py1 = (int16_t)(cy + r * sinf(ang));
        u8.drawLine(px0, py0, px1, py1);
        px0 = px1;
        py0 = py1;
    }
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
//  Aplica los moduladores de animación (_animOffX/Y, _animEscala,
//  _animEscalaY, _lidExtra) sobre una copia local de los parámetros.
// ================================================================
void Face::drawEye(U8G2 &u8, const EyeParams &pIn)
{
    // Copia con moduladores de animación aplicados
    EyeParams p = pIn;
    p.w    *= _animEscala;
    p.h    *= _animEscala * _animEscalaY;
    p.offX += _animOffX;
    p.offY += _animOffY;
    p.pTop += _lidExtra;

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

    case EyeStyle::ESPIRAL:
        drawEyeEspiral(u8, cx, cy, w);
        break;

    case EyeStyle::CIRCULO_BRILLO:
        drawEyeCirculoBrillo(u8, cx, cy, w);
        break;

    case EyeStyle::MEDIALUNA:
        drawEyeMedialuna(u8, cx, cy, w);
        break;
    }
}

// ----------------------------------------------------------------
//  drawHeart — corazón de tamaño variable (partículas de AMOR)
//  Dos discos + un triángulo invertido; r = radio de los lóbulos
// ----------------------------------------------------------------
static void drawHeart(U8G2 &u8, int16_t cx, int16_t cy, int16_t r)
{
    if (r < 2) r = 2;
    u8.setDrawColor(1);
    u8.drawDisc((u8g2_uint_t)(cx - r + 1), (u8g2_uint_t)cy, (u8g2_uint_t)r);
    u8.drawDisc((u8g2_uint_t)(cx + r - 1), (u8g2_uint_t)cy, (u8g2_uint_t)r);
    u8.drawTriangle(cx - r * 2 + 1, cy + 1,
                    cx + r * 2 - 1, cy + 1,
                    cx,             cy + r * 2);
}

// ----------------------------------------------------------------
//  Face::renderParticulas — corazones, Zzz, lágrima
// ----------------------------------------------------------------
void Face::renderParticulas(U8G2 &u8)
{
    for (uint8_t i = 0; i < ANIM_PARTICULAS_MAX; i++) {
        const Particula &p = _partes[i];
        if (p.tipo == PART_LIBRE) continue;

        uint32_t edad = _lastNow - p.nacioMs;
        float    vida = (float)edad / (float)p.vidaMs;   // 0..1
        int16_t  px = (int16_t)p.x;
        int16_t  py = (int16_t)p.y;

        switch (p.tipo) {

        case PART_CORAZON: {
            // Crece y se achica: seno de medio ciclo sobre la vida
            int16_t r = (int16_t)(2.0f + 2.0f * sinf(vida * 3.1416f));
            drawHeart(u8, px, py, r);
            break;
        }

        case PART_ZZZ:
            // Va creciendo con la edad: "z" chica → "Z" grande
            u8.setDrawColor(1);
            if (vida < 0.45f) {
                u8.setFont(u8g2_font_4x6_tf);
                u8.drawStr(px, py, "z");
            } else {
                u8.setFont(u8g2_font_5x8_tf);
                u8.drawStr(px, py, "Z");
            }
            break;

        case PART_LAGRIMA: {
            // Crece en el borde del ojo, luego cae (updateParticulas)
            int16_t r = (edad < 600) ? (int16_t)(1 + edad / 300) : 2;
            u8.setDrawColor(1);
            u8.drawDisc((u8g2_uint_t)px, (u8g2_uint_t)py, (u8g2_uint_t)r);
            break;
        }
        }
    }
}

// ----------------------------------------------------------------
//  Face::renderExtras — adornos no-partícula por expresión
// ----------------------------------------------------------------
void Face::renderExtras(U8G2 &u8)
{
    switch (_expr) {

    case Expression::ENOJADO:
        // Rayitas de furia sobre los costados, parpadeando
        if ((_lastNow / 300) % 2 == 0) {
            u8.setDrawColor(1);
            // Izquierda: dos rayitas inclinadas
            u8.drawLine(10, 16, 16, 10);
            u8.drawLine(14, 20, 20, 14);
            // Derecha: espejadas
            u8.drawLine(112, 10, 118, 16);
            u8.drawLine(108, 14, 114, 20);
        }
        break;

    case Expression::ABURRIDO: {
        // "..." apareciendo de a un punto (ciclo de 4: 0-3 puntos)
        uint8_t puntos = (uint8_t)((_lastNow / 600) % 4);
        u8.setDrawColor(1);
        for (uint8_t i = 0; i < puntos; i++) {
            u8.drawDisc((u8g2_uint_t)(56 + i * 8), 56, 1);
        }
        break;
    }

    case Expression::SOSPECHOSO:
        // "?" flotando arriba a la derecha, con parpadeo y vaivén
        if ((_lastNow % 900) < 600) {
            float bob = sinf((float)_lastNow * 0.005f) * 2.0f;
            u8.setDrawColor(1);
            u8.setFont(u8g2_font_6x12_tf);
            u8.drawStr(110, (int16_t)(14 + bob), "?");
        }
        break;

    // ILUSIONADO, RISA y MAREADO: sin boca — la expresión la dan solo los ojos.
    // (RISA: arcos "^  ^" apretados + rebote enérgico; MAREADO: espirales + tambaleo;
    //  ILUSIONADO: discos grandes con estrella + mirada arriba.)

    default:
        break;
    }
}

// ----------------------------------------------------------------
//  Face::render  —  dibuja ojos + partículas + extras
// ----------------------------------------------------------------
void Face::render(U8G2 &u8)
{
    drawEye(u8, _leftCur);
    drawEye(u8, _rightCur);
    renderParticulas(u8);
    renderExtras(u8);
    u8.setDrawColor(1);
}
