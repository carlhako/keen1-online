/**
 * The Keen 1 v1.31 shareware files the page needs, as the game looks them up (upper case). The
 * installer's md5 list in `deploy/install.sh` names exactly these; `gamefiles.test.ts` keeps the two
 * in step. `CTLPANEL.CK1` and `SCORES.CK1` are left out on purpose: the game writes them itself.
 */
export const GAME_FILES: readonly string[] = [
  'EGAHEAD.CK1',
  'EGALATCH.CK1',
  'EGASPRIT.CK1',
  'ENDTEXT.CK1',
  'FINALE.CK1',
  'HELPTEXT.CK1',
  'KEEN1.EXE',
  ...Array.from({ length: 16 }, (_, i) => `LEVEL${String(i + 1).padStart(2, '0')}.CK1`),
  'LEVEL80.CK1',
  'LEVEL81.CK1',
  'LEVEL90.CK1',
  'PREVIEW2.CK1',
  'PREVIEW3.CK1',
  'PREVIEWS.CK1',
  'SOUNDS.CK1',
  'STORYTXT.CK1',
];
