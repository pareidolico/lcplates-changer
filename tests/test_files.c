/* Tests PlateChanger's file handling (vehicles.ide presets, PlateChanger.plates)
   on Linux, using a small stand-in windows.h. Run: tests/run.sh */
#include <windows.h>
#include <math.h>
#define memset pc_memset
#define memcpy pc_memcpy
#include "../src/platechanger.c"
#undef memset
#undef memcpy
#include <math.h>
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
static int feq(float a, float b) { return fabsf(a - b) < 1e-4f; }
int main(void)
{
    strcpy(g_logPath, "/tmp/pctest.log"); strcpy(g_platesPath, "/tmp/pctest.plates");
    remove("/tmp/pctest.plates"); remove("/tmp/pctest.log");
    /* a small license_plates section like LibertyCityPlates' own */
    {
        char path[512];
        system("mkdir -p /tmp/game");
        sprintf(path, "/tmp/game\\update\\common\\data\\vehicles.ide");   /* Windows-style path, as the plugin builds it */
        FILE *f = fopen(path, "wb");
        fputs("cars\r\nadmiral, admiral, car, ADMIRAL\r\nend\r\n"
              "license_plates\r\n# id parent chance ...\r\n"
              "0 23 84 2 3 1.4 0.48 0.0 -0.02 0.0 0.0 0.0 1.0 &&&*^^& 1 -2315.0 -750 -1000.0 2100.0\r\n"
              "2 null 0 0 0 1.0 0.43 0.0 -0.07 0.0352 0.1647 0.3019 1.0 &&&*^^^^ 0\r\n"
              "26 46 15 12 0 1.3 0.43 0.0 -0.07 0.114 0.176 0.341 1.0 &&&##^^^ 1 -750.0 3000.0 -3000.0 3000.0\r\n"
              "   34 47 80 19 2 1.4 0.475 0.0 0.1 0.016 0.09 0.349 1.0 ^^^(&&& 0\r\n"
              "48 34 4 24 4 1.4 0.46 0.0 -0.18 0.09 0.09 0.09 1.0 &&&#^^^ 0\r\n"
              "40 0 50 12 9 9.9 9.9 9.9 9.9 0.0 0.0 0.0 1.0 &&&##^^^ 0\r\n"     /* later line, same picture: ignored */
              "end\r\nused_plates\r\nvoodoo 48\r\nend\r\n", f);
        fclose(f);
    }
    load_styles();
    struct { int pic, font; float sx, sy, ox, oy, r, g, b; } exp[] = {
        { 2, 3, 1.4f, 0.48f, 0.0f, -0.02f, 0.0f, 0.0f, 0.0f },
        { 0, 0, 1.0f, 0.43f, 0.0f, -0.07f, 0.0352f, 0.1647f, 0.3019f },
        { 12, 0, 1.3f, 0.43f, 0.0f, -0.07f, 0.114f, 0.176f, 0.341f },
        { 19, 2, 1.4f, 0.475f, 0.0f, 0.1f, 0.016f, 0.09f, 0.349f },
        { 24, 4, 1.4f, 0.46f, 0.0f, -0.18f, 0.09f, 0.09f, 0.09f },
    };
    for (unsigned i = 0; i < sizeof exp / sizeof exp[0]; i++) {
        Style *s = &g_styles[exp[i].pic];
        CHECK(s->have, "picture %d has no style", exp[i].pic);
        CHECK(s->font == exp[i].font, "picture %d font %d != %d", exp[i].pic, s->font, exp[i].font);
        CHECK(feq(s->sx, exp[i].sx) && feq(s->sy, exp[i].sy) && feq(s->ox, exp[i].ox) && feq(s->oy, exp[i].oy),
              "picture %d layout %f %f %f %f", exp[i].pic, s->sx, s->sy, s->ox, s->oy);
        CHECK(feq(s->r, exp[i].r) && feq(s->g, exp[i].g) && feq(s->b, exp[i].b), "picture %d tint", exp[i].pic);
    }
    build_lists();
    CHECK(g_npic == 5 && g_picList[0] == 0 && g_picList[1] == 2 && g_picList[4] == 24, "picture list (%d)", g_npic);
    CHECK(g_nfont == 4 && g_fontList[0] == 0 && g_fontList[1] == 2 && g_fontList[3] == 4, "font list (%d)", g_nfont);
    CHECK(step(-1, 1, g_picList, g_npic) == 0 && step(-1, -1, g_picList, g_npic) == 24, "first press starts at an end");
    CHECK(step(2, 1, g_picList, g_npic) == 12 && step(12, -1, g_picList, g_npic) == 2, "stepping skips unused pictures");
    CHECK(step(24, 1, g_picList, g_npic) == 0 && step(0, -1, g_picList, g_npic) == 24, "stepping wraps around");
    CHECK(step(5, 1, g_picList, g_npic) == 12 && step(5, -1, g_picList, g_npic) == 2, "a value outside the list goes to the nearest");
    g_maxPic = 3; build_lists(); CHECK(g_npic == 4 && g_picList[3] == 3, "MaxPicture override");
    g_maxPic = -1; build_lists();
    CHECK(!g_styles[100].have, "picture 100 shouldn't exist");
    int nstyles = 0; for (int i = 0; i < 254; i++) nstyles += g_styles[i].have;
    printf("styles loaded: %d pictures\n", nstyles);

    /* records round trip */
    g_nrec = 2;
    Record *a = &g_rec[0]; memset(a, 0, sizeof *a);
    a->model = 3546358179u; a->col[0] = 1; a->col[1] = 2; a->col[2] = 113; a->col[3] = 0;
    strcpy(a->text, "DLW 796"); a->len = 7; a->pic = 24; a->font = -1;
    add_spot(a, -1234.5f, 678.9f, 12.3f); add_spot(a, 500.0f, -20.25f, 5.0f); add_spot(a, -1234.0f, 679.5f, 12.0f); /* same spot again */
    Record *b = &g_rec[1]; memset(b, 0, sizeof *b);
    b->model = 42; b->len = 0; b->pic = -1; b->font = 3;
    add_spot(b, 1, 2, 3);
    CHECK(a->nspots == 2, "duplicate spot should merge, got %d spots", a->nspots);
    CHECK(dist_ok(a->spot[0], -1234.0f, 679.5f, 12.0f, 1.0f), "re-used spot should move to the front");
    save_records();
    Record before[2]; memcpy(before, g_rec, sizeof before);
    g_nrec = 0; memset(g_rec, 0, sizeof g_rec);
    load_records();
    CHECK(g_nrec == 2, "loaded %d records", g_nrec);
    for (int i = 0; i < 2 && g_nrec == 2; i++) {
        Record *x = &before[i], *y = &g_rec[i];
        CHECK(x->model == y->model, "model"); CHECK(!memcmp(x->col, y->col, sizeof x->col), "colours");
        CHECK(x->len == y->len && !strcmp(x->text, y->text), "text '%s'(%d) vs '%s'(%d)", x->text, x->len, y->text, y->len);
        CHECK(x->pic == y->pic && x->font == y->font, "pic/font"); CHECK(x->nspots == y->nspots && !memcmp(x->spot, y->spot, sizeof x->spot), "spots");
    }
    CarId id = { 3546358179u, {1, 2, 113, 0}, -1230.0f, 680.0f, 12.0f };
    CHECK(same_car(&g_rec[0], &id), "same car should match");
    id.col[2] = 5; CHECK(!same_car(&g_rec[0], &id), "different colour shouldn't match");
    CHECK(dist_ok(g_rec[0].spot[0], -1230.0f, 680.0f, 12.0f, MATCH_DIST), "4.5 m away should match a spot");
    CHECK(!dist_ok(g_rec[0].spot[0], -1220.0f, 680.0f, 12.0f, MATCH_DIST), "14 m away shouldn't match");
        printf(fails ? "\n%d FAILURE(S)\n" : "\nall checks passed\n", fails);
    return fails != 0;
}
