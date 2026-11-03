#ifndef FW_NN_DEVICE_H
#define FW_NN_DEVICE_H
#include "fw_sparse.h"

#define FW_NN_MMIO_ID 0x4e4e3137u
#define FW_NN_MMIO_ABI 14u
#define FW_NN_MMIO_CAPS 262143u
#ifndef FW_NN_DW_COMMAND_BATCH
#define FW_NN_DW_COMMAND_BATCH 1
#endif
#ifndef FW_NN_CHAIN_DW_BATCH
#define FW_NN_CHAIN_DW_BATCH 1
#endif
#ifndef FW_NN_DW_TAIL_BATCH
#define FW_NN_DW_TAIL_BATCH 1
#endif
#ifndef FW_NN_DW_RESIDUE_BATCH
#define FW_NN_DW_RESIDUE_BATCH 1
#endif
/* Actual64-word banks support contiguous encoder residency without the
 * tighter C32-word joint layout/spill. All four banks keep PW2[0,12),
 * STEM uses three banks[12,39), transient DW groups use[40,56).
 * Biases are separate and still reload when the layer changes. */
enum {FW_NN_ENCODER_STEM=0u,FW_NN_ENCODER_DW1=1u,FW_NN_ENCODER_PW2=2u};
enum {FW_NN_ENCODER_STEM_BASE=12u,FW_NN_ENCODER_DW_BASE=40u,FW_NN_ENCODER_PW_BASE=0u};
enum {FW_NN_CORE=0u,FW_NN_DMA=1u};
enum {FW_NN_REG_ID=0u,FW_NN_REG_ABI=4u,FW_NN_REG_CAPS=8u,FW_NN_REG_POISON=12u,
 FW_NN_REG_CORE_STATUS=0x20u,FW_NN_REG_CORE_TICKET=0x24u,FW_NN_REG_CORE_FAULT=0x28u,
 FW_NN_REG_CORE_ISSUE=0x2cu,FW_NN_REG_CORE_CONTROL=0x30u,FW_NN_REG_TERMS=0x34u,FW_NN_REG_READS=0x38u,FW_NN_REG_ZEROS=0x3cu,
 FW_NN_REG_DMA_STATUS=0x40u,FW_NN_REG_DMA_TICKET=0x44u,FW_NN_REG_DMA_FAULT=0x48u,
 FW_NN_REG_DMA_ISSUE=0x4cu,FW_NN_REG_DMA_CONTROL=0x50u,FW_NN_REG_DMA_COMPLETED=0x54u,
 FW_NN_REG_CORE_COMMAND=0x100u,FW_NN_REG_DMA_COMMAND=0x180u,FW_NN_REG_SCALAR_SUMMARY=0x200u,FW_NN_REG_DW_SUMMARY=0x240u,FW_NN_REG_TILES=0x60u};
enum {FW_NN_HW_BUSY=1u,FW_NN_HW_DRAINED=2u,FW_NN_HW_CANCELLED=4u,FW_NN_HW_FAULT=8u};
enum {FW_NN_HW_START=1u,FW_NN_HW_CANCEL=2u,FW_NN_HW_ACK=4u};
enum {FW_NN_DMA_RAW_LOAD=0u,FW_NN_DMA_RAW_EXPORT=1u,FW_NN_DMA_WEIGHT=2u,FW_NN_DMA_BIAS=3u};
enum {FW_NN_DMA_PACK_I16=1u<<7};
enum {FW_NN_RELU=1u<<2,FW_NN_DEPTHWISE=1u<<3,FW_NN_DW_PAIR=1u<<4,FW_NN_DW_QUAD=1u<<5,
 FW_NN_STEM=1u<<6,FW_NN_PW_LINK=1u<<7,FW_NN_DW_PACK=1u<<8,FW_NN_DW_STORE=1u<<9,
 FW_NN_PW_RAW=1u<<10,FW_NN_RESULT_STORE=1u<<11,FW_NN_RESULT_GROUP4=1u<<12,FW_NN_PW_BANK23=1u<<13,FW_NN_RESIDUAL=1u<<14};
/* Little-endian32-bit words. Both descriptors start with frame_lo,frame_hi,
 * epoch,attempt,sequence. Their registered physical DDR addresses are not
 * PS pointers. Region/cache/generation leases remain the caller's duty.
 * Core words5..7: flags; pixels5/outputs5/channels7/weight_base6/shift6;
 * output_fraction4/storage_fraction4/DW_groups3/DW_terms7/recipe2.
 * Words8..11 are four14-bit DW cell bases;12 pitch9/step3.
 * Words13 and16 are PW/result cell_base14/pitch9;14..15 channel mask64.
 * Words17..20 are RAW row_word_base12/skew4/valid1.
 * Word21 origin11/width9/step3;22 advance10;23 tiles-minus-one5.
 * ABI14 also batches recipe1's complete set of2/4 residues: tiles equals
 * input step and store step, advance is twice that step, no short tail.
 * Each <=32term child keeps its arithmetic; origin and active store bases
 * advance ONE cell per child. The union spans2*terms*tiles cells. Source
 * rows, coefficients and whole-result ownership remain live through ACK.
 * ABI13 word23 bits5..11 optionally give the final DW child's term count.
 * Zero retains uniform children; a nonzero count must be smaller than the
 * main count and requires >=2 DW children. All other word23 bits are zero.
 * Only the last child shrinks; its origin/base still advances by the main
 * size for every preceding child. Whole spans include the actual tail.
 * ABI12 permits canonical DW_QUAD+DW_STORE batches (flags553), recipe0/2,
 * step/store_step1, advance2/4. Each child keeps <=16/32terms and64stored
 * rows; later children advance origin by terms*advance and each store base
 * by2*terms. Entire source/result spans must be disjoint and bounded.
 * ABI3 batches support only PW/PW-link with RAW and result stores. Source
 * and result bases advance pixels cells per microtile; each channel pitch
 * must cover the whole block. Exact in-place or disjoint spans only.
 * Channel mask must cover the ENTIRE block and have no bits beyond CI;
 * enabling every actual channel is always safe.
 * TILES returns total in high16/completed in low16. Summary reads reject
 * multi-tile commands: the last microtile bitmap is not a block bitmap.
 * DMA words5..11: target,DDR_address,word_count,RAW_word_address,
 * weight_bank2/weight_address6,bias_address,reserved_zero.
 * ABI2 RAW multirow: word7 is words/row; word9 RAW stride in words,
 * word10 DDR stride in bytes, word11 rows-minus-one (1..127). With word11
 * zero the old single-span encoding is unchanged. Both pitches must cover
 * the row; the entire descriptor is validated before any side effect.
 * ABI4 PACK_I16: word11 bit7=1, low2bits planes-minus-one (0..3).
 * Word9 pixels9/first_lane2/XOR_skew4/source_halfword2. Word10 is DDR
 * plane stride; word7 must equal ceil((source_halfword+pixels)/4).
 * Native CHW I16 input fills1..4 consecutive lanes of each RAW word,
 * preserving every other byte; (RAW_base+pixel) low4bits XOR the skew.
 * A source word counts as completed only after all requested cells commit.
 * Cancelled partial words can still modify RAW: never publish failed rows.
 * DDR still transfers whole64-bit words; caller owns source row-edge
 * words/cache lines and the entire destination until successful completion.
 * ABI5 WEIGHT multirow: target2, word11 banks-minus-one (1..3),
 * word9 starting_bank2/weight_address6, word10 DDR bank stride in bytes.
 * Each consecutive bank receives word7 words at the SAME local address.
 * The whole bank/address extent is validated before any bus or local write.
 * Cancellation may leave a dirty prefix of banks; reload before computing.
 * ABI6 GROUP4 result flag12 is supported only with nativeSTEM+RESULT_STORE.
 * Result base is a4-cell-aligned I16 index; pitch is WORDS between groups
 * of four channels. Cell address=base+(channel/4)*pitch*4+pixel*4+channel%4.
 * Whole affected words are leased until ACK; unused lanes are preserved.
 * Native source spans and result spans must be disjoint. Other scalar
 * results keep original CHW cell-pitch interpretation.
 * ABI7 permits GROUP4 nativeSTEM batches in word23 (1..32 tiles of8).
 * ABI8 also permits PW_RAW+RESULT_STORE+GROUP4, without PW_LINK.
 * CHW source and GROUP4 result WORD spans must be disjoint even when
 * base and pitch match. Result advances four cells per pixel; source
 * retains its original one-cell advance. All batch spans stay leased.
 * Each internal tile advances input origin by16 and result base by32cells.
 * Source/result spans are checked and leased across every internal ACK;
 * all three input rows and coefficients must survive the complete batch.
 * Other STEM batches remain unsupported; ABI12 DW rules are above.
 * Summary reads stay disabled for every multi-child batch.
 * ABI9 BANK23 selects banks2/3 for mode1 PW (<=8outputs), including
 * PWlink tail coefficients inbank2. It cannot accompany STEM or DW.
 * Weight addresses, DMA bank numbers and all other modes stay unchanged.
 * ABI10 RESIDUAL flag14 adds packed Q12 skip values AFTER the original PW
 * quantization/ReLU and clamps to[0,24576]. Only PW_RAW+RESULT_STORE,
 * storage_fraction12, <=16outputs, no STEM/DW/LINK. Words8/9 are
 * RAW row0/row1 word bases12;10 width9;11 group-of-four pitch9 in words;
 * 12 starting column9. A command/batch covers at most two skip rows.
 * Batches advance row/column while retaining the complete skip spans.
 * ABI11 also permits RESIDUAL+GROUP4: the existing masked scalar writer
 * stores the final Q12 value directly in grouped rows, preserving unused
 * lanes. Grouped result base is four-cell aligned; pitch is words. Entire
 * source, skip and result spans survive internal ACKs; result must be
 * disjoint from both sources. Arithmetic and descriptor size are unchanged.
 * Source centers are the caller's responsibility; loads cannot overwrite
 * live skip words. No new DMA, MAC array or RAW storage is implied.
 */
typedef struct {uint32_t words[24];} fw_nn_core_command;
typedef struct {uint32_t words[12];} fw_nn_dma_command;
_Static_assert(sizeof(fw_nn_core_command)==96u,"NN core wire ABI");
_Static_assert(sizeof(fw_nn_dma_command)==48u,"NN DMA wire ABI");
typedef struct {
 void *user;
 uint32_t (*read32)(void *,uint32_t);
 void (*write32)(void *,uint32_t,uint32_t);
 void (*barrier)(void *);
 int (*map)(void *,const void *,uint32_t,uint32_t *);
} fw_nn_mmio_io;
typedef struct {
 uint32_t drained,cancelled,fault,poisoned,completed_words;
 uint32_t accepted_terms,committed_terms,raw_reads,raw_delivered,raw_zeros;
 uint32_t completed_tiles,total_tiles;
} fw_nn_progress;
typedef struct {
 fw_nn_mmio_io io;uint32_t magic,state[2],next_ticket[2],ticket[2],last_completed[2],cancel_sent[2],owner[2][5],reservation,block_ticket;
 uint32_t descriptor_reuse,descriptor_start,descriptor_valid[2],core_payload[19],dma_payload[7];
} fw_nn_mmio;
#ifndef FW_NN_DESCRIPTOR_REUSE
#define FW_NN_DESCRIPTOR_REUSE 1
#endif
#ifndef FW_NN_DMA_RECEIPT_TRANSACTION
#define FW_NN_DMA_RECEIPT_TRANSACTION 1
#endif
/* Zero-initialize once before workers start. Init never resets/ACKs an
 * unknown job; it adopts the idle hardware's last tickets without reuse.
 * Each unit is serialized with lock-free32-bit atomics across both A9s.
 */
int fw_nn_mmio_init(fw_nn_mmio *,const fw_nn_mmio_io *);
/* Explicit closed-endpoint contract, disabled by init. Enable only when all
 * command-register writes use this one driver. Both units must be idle and
 * ticket history intact. Calling again invalidates both software shadows.
 * Disable BEFORE handing registers to a raw writer; reinit/rebuild payload
 * ownership after foreign compute or reset. A mere unannounced shadow write
 * cannot be detected by tickets and violates enabled-mode ownership.
 * Both ticket histories are checked before every issue, including the first
 * CORE after DMA preparation. Only the short issue transaction is serialized;
 * already running CORE/DMA work retains its existing overlap semantics.
 * History loss returns STALE without issue/ACK or silently adopting old RAW. */
int fw_nn_mmio_descriptor_reuse(fw_nn_mmio *,unsigned);
int fw_nn_mmio_map(fw_nn_mmio *,const void *,uint32_t,uint32_t *);
/* Validate every separate row through the registered mapping; require a
 * linear physical pitch, <=128 rows and <=32KiB total useful bytes. Cache
 * and generation leases still belong to the caller, including row edges. */
int fw_nn_mmio_map_rows(fw_nn_mmio *,const void *,uint32_t,uint32_t,uint32_t,uint32_t *);
/* Build words5..11 after mapping every aligned native plane. Owner words
 * remain unchanged; no descriptor update on failure and no MMIO issue.
 * Cache/generation leases remain with the caller. A one-plane pitch0 is
 * allowed; multiple planes require an aligned, nonoverlapping byte pitch. */
int fw_nn_mmio_set_pack_i16(fw_nn_mmio *,fw_nn_dma_command *,const int16_t *,uint32_t pixels,uint32_t planes,uint32_t plane_pitch,uint32_t raw_base,uint32_t first_lane,uint32_t skew);
/* Place an already configured encoder core descriptor, or build one
 * weight-bank DMA descriptor from already packed immutable coefficients.
 * No MMIO is issued; failures leave the descriptor unchanged. The caller
 * owns cache/generation tags and must invalidate residency after foreign
 * writes. This does not claim whole-graph or automatic bias residency. */
int fw_nn_encoder_place(fw_nn_core_command *,unsigned layer);
int fw_nn_encoder_weight_command(fw_nn_mmio *,fw_nn_dma_command *,const uint64_t *,unsigned layer,unsigned bank);
int fw_nn_mmio_start_core(fw_nn_mmio *,const fw_nn_core_command *,uint32_t *);
int fw_nn_mmio_start_dma(fw_nn_mmio *,const fw_nn_dma_command *,uint32_t *);
/* Poll never ACKs. Any nonzero return leaves ownership intact. FW_PENDING
 * means the receipt changed while being read; retry without publishing.
 * Poison is diagnostic only and cannot be interpreted as drained.
 */
int fw_nn_mmio_poll(fw_nn_mmio *,unsigned,uint32_t,fw_nn_progress *);
int fw_nn_mmio_cancel(fw_nn_mmio *,unsigned,uint32_t);
int fw_nn_mmio_ack(fw_nn_mmio *,unsigned,uint32_t);
/* Only valid after a stable successful single-tile drained poll and before ACK.
 * Exactly9 scalar or40 DW words; nonzero return invalidates the output.
 */
int fw_nn_mmio_summary(fw_nn_mmio *,uint32_t,unsigned,uint32_t *,unsigned);

enum {FW_NN_BLOCK_TRANSFERS=9u};
#ifndef FW_NN_COMPACT_PLAN
#define FW_NN_COMPACT_PLAN 1
#endif
typedef struct {
 fw_nn_core_command core;
 fw_nn_dma_command prepare[FW_NN_BLOCK_TRANSFERS],export[FW_NN_BLOCK_TRANSFERS];
 uint32_t prepare_count,export_count,dma_only;
} fw_nn_block_plan;
typedef struct {
 fw_nn_mmio *device;fw_nn_block_plan plan;
 uint32_t gate,active,identity,phase,index,ticket[2],dma_owned,core_owned,cancel_requested,error,done,prepared_words,exported_words;
 fw_nn_progress core_progress;
} fw_nn_block;
typedef struct {
 uint32_t done,result,cancelled,prepared_words,exported_words,phase;
 fw_nn_progress core;
} fw_nn_block_progress;
/* Zero-initialize block once. Begin owns a snapshot of core/flags and all
 * used transfer descriptors; unused array tails are neither read nor used.
 * dma_only=1 requires at least one transfer, valid owner in core.words[0:4]
 * and zero core.words[5:23]. No compute command is issued in that mode.
 * all descriptors must have the SAME five owner words. Both units stay
 * reserved until release, excluding direct driver calls and other blocks.
 * Physical region, source/destination/cache-line and sharedNN/BOX/SGM
 * leases remain with the caller across EVERY pending call. Pre-clean DMA
 * sources/destinations before begin. After done/result0, acquire output
 * cache lines and recheck frame/generation before publishing any row.
 * No activation copy or implicit cache/publication operation occurs here.
 */
int fw_nn_block_begin(fw_nn_block *,fw_nn_mmio *,const fw_nn_block_plan *,uint32_t *);
/* One bounded event/transition per call; no device wait loop. FW_OK means
 * a stable terminal receipt is available in out, whose result says success
 * or failure. FW_PENDING/FW_BUSY/other errors NEVER authorize release or
 * publication. Poll failures remain pending and request cancel until actual
 * drain; poisoned accepted DMA never becomes done. */
int fw_nn_block_step(fw_nn_block *,uint32_t,fw_nn_block_progress *);
int fw_nn_block_cancel(fw_nn_block *,uint32_t);
int fw_nn_block_summary(fw_nn_block *,uint32_t,unsigned,uint32_t *,unsigned);
/* Terminal core receipt and reservations remain owned until this call.
 * A nonzero release result leaves ownership intact; retry/diagnose. */
int fw_nn_block_release(fw_nn_block *,uint32_t);
#endif
