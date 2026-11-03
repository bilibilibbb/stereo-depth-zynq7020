`timescale 1ns/1ps
// OV5640: 7-bit address 0x3c, 16-bit register, 8-bit value.
// Read uses register-pointer WRITE, STOP, bus-free interval, then READ + NACK.
// Open-drain outputs: 1 = pull low, 0 = release. ACK is checked at EVERY byte.
module sccb_master #(
    parameter QUARTER_CYCLES=125, parameter TIMEOUT_CYCLES=250000
)(
    input wire clk, input wire resetn, input wire start,
    input wire read_op, input wire recover_op,
    input wire [15:0] reg_addr, input wire [7:0] write_data,
    input wire scl_in, input wire sda_in,
    output reg scl_low, output reg sda_low,
    output reg busy, output reg done, output reg [7:0] error,
    output reg [7:0] read_data,
    output wire [31:0] dbg_live, output wire [31:0] dbg_age, output wire [31:0] dbg_divider,
    output wire [31:0] dbg_fault, output wire [31:0] dbg_fault_age, output wire [31:0] dbg_fault_divider,
    output wire [31:0] dbg_scl_rises, output wire [31:0] dbg_scl_falls, output wire [31:0] dbg_fault_count
);
    localparam IDLE=0,FREE=1,START_HOLD=2,TX_SETUP=3,TX_RAISE=4,TX_HIGH=5,TX_FALL=6,
        ACK_SETUP=7,ACK_RAISE=8,ACK_HIGH=9,ACK_FALL=10,
        STOP_LOW=11,STOP_RAISE=12,STOP_HIGH=13,STOP_DONE=14,
        RX_SETUP=15,RX_RAISE=16,RX_HIGH=17,RX_FALL=18,
        NACK_SETUP=19,NACK_RAISE=20,NACK_HIGH=21,NACK_FALL=22,
        CLEAR_LOW=23,CLEAR_RAISE=24,CLEAR_HIGH=25,CLEAR_FALL=26;
    reg [5:0] state;
    reg [31:0] divider, age;
    reg [15:0] addr_latched;
    reg [7:0] data_latched, tx_byte, rx_byte;
    reg [2:0] bit_index, byte_index;
    reg [3:0] clear_count;
    reg do_read, restart_read;
    (* ASYNC_REG="TRUE" *) reg [1:0] scl_sync,sda_sync;
    always @(posedge clk) begin
        if (!resetn) begin scl_sync<=3; sda_sync<=3; end
        else begin scl_sync<={scl_sync[0],scl_in}; sda_sync<={sda_sync[0],sda_in}; end
    end
    always @(posedge clk) begin
        if (!resetn) begin
            state<=IDLE; divider<=0; age<=0; scl_low<=0; sda_low<=0;
            busy<=0; done<=0; error<=0; read_data<=0;
            addr_latched<=0; data_latched<=0; tx_byte<=0; rx_byte<=0;
            bit_index<=7; byte_index<=0; do_read<=0; restart_read<=0; clear_count<=0;
        end else if (!busy) begin
            scl_low<=0; sda_low<=0; divider<=0; age<=0;
            if (start) begin
                addr_latched<=reg_addr; data_latched<=write_data; do_read<=read_op;
                busy<=1; done<=0; error<=0; read_data<=0; restart_read<=0;
                byte_index<=0; tx_byte<=8'h78; bit_index<=7; rx_byte<=0; clear_count<=0;
                state<=recover_op ? CLEAR_LOW : FREE;
            end
        end else if (age>=TIMEOUT_CYCLES-1) begin
            busy<=0; done<=1; error<=8'h80; scl_low<=0; sda_low<=0; state<=IDLE;
        end else begin
            age<=age+1;
            if (divider==QUARTER_CYCLES-1) begin
                divider<=0;
                case(state)
                    FREE: begin
                        scl_low<=0; sda_low<=0;
                        if (scl_sync[1] && sda_sync[1]) begin sda_low<=1; state<=START_HOLD; end
                    end
                    START_HOLD: begin scl_low<=1; state<=TX_SETUP; end
                    TX_SETUP: begin sda_low<=!tx_byte[bit_index]; state<=TX_RAISE; end
                    TX_RAISE: begin scl_low<=0; state<=TX_HIGH; end
                    TX_HIGH: if (scl_sync[1]) state<=TX_FALL;
                    TX_FALL: begin
                        scl_low<=1;
                        if (bit_index==0) state<=ACK_SETUP;
                        else begin bit_index<=bit_index-1; state<=TX_SETUP; end
                    end
                    ACK_SETUP: begin sda_low<=0; state<=ACK_RAISE; end
                    ACK_RAISE: begin scl_low<=0; state<=ACK_HIGH; end
                    ACK_HIGH: if (scl_sync[1]) begin
                        if (sda_sync[1]) error<={5'b0,byte_index}+8'd1;
                        state<=ACK_FALL;
                    end
                    ACK_FALL: begin
                        scl_low<=1; bit_index<=7;
                        if (error!=0) state<=STOP_LOW;
                        else case(byte_index)
                            0: begin byte_index<=1; tx_byte<=addr_latched[15:8]; state<=TX_SETUP; end
                            1: begin byte_index<=2; tx_byte<=addr_latched[7:0]; state<=TX_SETUP; end
                            2: if (do_read) begin restart_read<=1; state<=STOP_LOW; end
                               else begin byte_index<=3; tx_byte<=data_latched; state<=TX_SETUP; end
                            3: state<=STOP_LOW;
                            4: begin rx_byte<=0; state<=RX_SETUP; end
                            default: begin error<=8'h81; state<=STOP_LOW; end
                        endcase
                    end
                    RX_SETUP: begin sda_low<=0; state<=RX_RAISE; end
                    RX_RAISE: begin scl_low<=0; state<=RX_HIGH; end
                    RX_HIGH: if (scl_sync[1]) begin rx_byte[bit_index]<=sda_sync[1]; state<=RX_FALL; end
                    RX_FALL: begin
                        scl_low<=1;
                        if (bit_index==0) state<=NACK_SETUP;
                        else begin bit_index<=bit_index-1; state<=RX_SETUP; end
                    end
                    NACK_SETUP: begin sda_low<=0; state<=NACK_RAISE; end
                    NACK_RAISE: begin scl_low<=0; state<=NACK_HIGH; end
                    NACK_HIGH: if (scl_sync[1]) state<=NACK_FALL;
                    NACK_FALL: begin scl_low<=1; read_data<=rx_byte; state<=STOP_LOW; end
                    STOP_LOW: begin scl_low<=1; sda_low<=1; state<=STOP_RAISE; end
                    STOP_RAISE: begin scl_low<=0; state<=STOP_HIGH; end
                    STOP_HIGH: if (scl_sync[1]) begin sda_low<=0; state<=STOP_DONE; end
                    STOP_DONE: if (restart_read && error==0) begin
                        restart_read<=0; byte_index<=4; tx_byte<=8'h79; bit_index<=7; state<=FREE;
                    end else begin
                        if (!sda_sync[1] && error==0) error<=8'h82;
                        busy<=0; done<=1; state<=IDLE;
                    end
                    CLEAR_LOW: begin scl_low<=1; sda_low<=0; state<=CLEAR_RAISE; end
                    CLEAR_RAISE: begin scl_low<=0; state<=CLEAR_HIGH; end
                    CLEAR_HIGH: if (scl_sync[1]) state<=CLEAR_FALL;
                    CLEAR_FALL: begin
                        scl_low<=1;
                        if (clear_count==8) state<=STOP_LOW;
                        else begin clear_count<=clear_count+1; state<=CLEAR_LOW; end
                    end
                    default: begin error<=8'h81; state<=STOP_LOW; end
                endcase
            end else divider<=divider+1;
        end
    end
    // Observation only: no debug register feeds the SCCB controller.
    // [5:0]state [6]SCLdrive [7]SDAdrive [8]SCLsync [9]SDAsync
    // [10]busy [11]done [12]read [13]restart [16:14]bit [19:17]byte
    // [23:20]clear count [31:24]error. Fault values are BEFORE timeout release.
    assign dbg_live={error,clear_count,byte_index,bit_index,restart_read,do_read,
                     done,busy,sda_sync[1],scl_sync[1],sda_low,scl_low,state};
    assign dbg_age=age;
    assign dbg_divider=divider;
    // These optional outputs have no consumer in the minimal production core.
    assign dbg_fault=0,dbg_fault_age=0,dbg_fault_divider=0;
    assign dbg_scl_rises=0,dbg_scl_falls=0,dbg_fault_count=0;
endmodule
