/* shrine.h - the shrines (the rest of CAST2.OVL): the eight shrines of virtue and the Shrine of
 * the Codex. Parameter order follows the Ghidra signatures.
 *
 * A visit is a small scene on an 11 x 11 map from MISCMAPS.DAT (0xB0 the shrine, 0x160 the
 * Codex), with location 0xFF and the map's objects put aside; the texts come from MISCMSG.DAT at
 * 0x3AB. Meditating with the right virtue and mantra gives a sacred quest (a bit in
 * sacred_quest); the Codex then answers it (a bit in codex_answered) and the shrine rewards the
 * return. */
#ifndef U5_SHRINE_H
#define U5_SHRINE_H

void shrine_place_urns(int count);           /* eaf4: the ashes of dead companions */
void virtue_shrine(void);                    /* eb46 */
void codex_shrine(void);                     /* ef04 */
void shrine_walk_step(void);                 /* f044 */
void visit_shrine(void);                     /* f056 */

#endif
