// Stage A source basis SHA256: 890d03b8bd02afb90a4940d91f58c93c82e6be1957039aac3f487191dfd28f9d
`timescale 1ns/1ps
module capture_core #(
    parameter [31:0] BUILD_ID=0,
    parameter I2C_QUARTER=125, parameter I2C_TIMEOUT=250000,
    parameter SOURCE_WIDTH=640,SOURCE_HEIGHT=480,CAP_WIDTH=320,CAP_HEIGHT=240,CROP_X=160,CROP_Y=120
)(
    input wire clk,input wire resetn,
    input wire [31:0] s_awaddr,input wire s_awvalid,output wire s_awready,
    input wire [31:0] s_wdata,input wire [3:0] s_wstrb,input wire s_wvalid,output wire s_wready,
    output wire [1:0] s_bresp,output wire s_bvalid,input wire s_bready,
    input wire [31:0] s_araddr,input wire s_arvalid,output wire s_arready,
    output wire [31:0] s_rdata,output wire [1:0] s_rresp,output wire s_rvalid,input wire s_rready,
    input wire pclk0,input wire pclk1,input wire vs0,input wire vs1,input wire href0,input wire href1,
    input wire [7:0] data0,input wire [7:0] data1,
    input wire scl0,input wire sda0,input wire scl1,input wire sda1,
    input wire camera_reset_level,input wire camera_pwdn_level,input wire camera_xclk_level,
    output wire scl0_low,output wire sda0_low,output wire scl1_low,output wire sda1_low,
    output wire camera_reset_low,output wire camera_pwdn_low
);
    wire wr_en; wire [31:0] wr_addr,wr_data,rd_addr; wire [3:0] wr_strb;
    reg [31:0] rd_data; reg [1:0] wr_resp,rd_resp;
    axi_lite_regs bus(.clk(clk),.resetn(resetn),
        .s_awaddr(s_awaddr),.s_awvalid(s_awvalid),.s_awready(s_awready),
        .s_wdata(s_wdata),.s_wstrb(s_wstrb),.s_wvalid(s_wvalid),.s_wready(s_wready),
        .s_bresp(s_bresp),.s_bvalid(s_bvalid),.s_bready(s_bready),
        .s_araddr(s_araddr),.s_arvalid(s_arvalid),.s_arready(s_arready),
        .s_rdata(s_rdata),.s_rresp(s_rresp),.s_rvalid(s_rvalid),.s_rready(s_rready),
        .wr_en(wr_en),.wr_addr(wr_addr),.wr_data(wr_data),.wr_strb(wr_strb),.wr_resp(wr_resp),
        .rd_addr(rd_addr),.rd_data(rd_data),.rd_resp(rd_resp));
    wire [31:0] byte_mask={{8{wr_strb[3]}},{8{wr_strb[2]}},{8{wr_strb[1]}},{8{wr_strb[0]}}};
    wire [31:0] command=wr_data&byte_mask;
    reg [31:0] scratch,ticks,snap_ticks,snap_generation;
    reg [1:0] io_control;
    reg [15:0] sensor_addr;
    reg [7:0] sensor_wdata;
    reg [1:0] sensor_start;
    reg sensor_read,sensor_recover;
    wire busy0,busy1,done0,done1;
    wire [7:0] error0,error1,result0,result1;
    assign camera_reset_low=!io_control[0];
    assign camera_pwdn_low=io_control[1];
    (* ASYNC_REG="TRUE" *) reg [6:0] io_meta,io_sync;
    (* ASYNC_REG="TRUE" *) reg [15:0] data_meta,data_sync;
    wire [31:0] f0,t0,period0,lines0,current0,pc0,f1,t1,period1,lines1,current1,pc1;
    wire [1:0] level0,level1;
    reg [31:0] snap0[0:6]; reg [31:0] snap1[0:6];
    integer i;
    camera_monitor mon0(.clk(clk),.resetn(resetn),.pclk(pclk0),.vs(vs0),.href(href0),.ticks(ticks),
        .frames(f0),.last_frame_tick(t0),.period_ticks(period0),.last_lines(lines0),
        .current_lines(current0),.pclk_count(pc0),.levels(level0));
    camera_monitor mon1(.clk(clk),.resetn(resetn),.pclk(pclk1),.vs(vs1),.href(href1),.ticks(ticks),
        .frames(f1),.last_frame_tick(t1),.period_ticks(period1),.last_lines(lines1),
        .current_lines(current1),.pclk_count(pc1),.levels(level1));
    sccb_master #(.QUARTER_CYCLES(I2C_QUARTER),.TIMEOUT_CYCLES(I2C_TIMEOUT)) cam0_cfg(
        .clk(clk),.resetn(resetn),.start(sensor_start[0]),.read_op(sensor_read),.recover_op(sensor_recover),
        .reg_addr(sensor_addr),.write_data(sensor_wdata),.scl_in(scl0),.sda_in(sda0),
        .scl_low(scl0_low),.sda_low(sda0_low),.busy(busy0),.done(done0),.error(error0),.read_data(result0));

    sccb_master #(.QUARTER_CYCLES(I2C_QUARTER),.TIMEOUT_CYCLES(I2C_TIMEOUT)) cam1_cfg(
        .clk(clk),.resetn(resetn),.start(sensor_start[1]),.read_op(sensor_read),.recover_op(sensor_recover),
        .reg_addr(sensor_addr),.write_data(sensor_wdata),.scl_in(scl1),.sda_in(sda1),
        .scl_low(scl1_low),.sda_low(sda1_low),.busy(busy1),.done(done1),.error(error1),.read_data(result1));

    // Legacy ROI and SDB1 diagnostics are absent: their addresses return DECERR.
    always @* begin
        wr_resp=2'b11;
        case(wr_addr)
            32'h00c,32'h014,32'h018,32'h020,32'h024: wr_resp=0;
            32'h028: begin
                wr_resp=0;
                if ((command[0] && (busy0 || sensor_start[0])) ||
                    (command[1] && (busy1 || sensor_start[1]))) wr_resp=2'b10;
            end
            32'h000,32'h004,32'h008,32'h010,32'h01c,32'h02c,32'h030,32'h034,32'h038,32'h03c,
            32'h100,32'h104,32'h108,32'h10c,32'h110,32'h114,32'h118,
            32'h140,32'h144,32'h148,32'h14c,32'h150,32'h154,32'h158: wr_resp=2'b10;
        endcase
        if (wr_addr[1:0]!=0) wr_resp=2'b10;
    end
    always @* begin
        rd_resp=0; rd_data=0;
        case(rd_addr)
            32'h000: rd_data=32'h53544742;
            32'h004: rd_data=32'h00010000;
            32'h008: rd_data=BUILD_ID;
            32'h00c: rd_data=scratch;
            32'h010: rd_data=snap_ticks;
            32'h014: rd_data={30'b0,io_control};
            32'h018: rd_data=snap_generation;
            32'h01c: rd_data=ticks;
            32'h020: rd_data={16'b0,sensor_addr};
            32'h024: rd_data={24'b0,sensor_wdata};
            32'h028: rd_data=0;
            32'h02c: rd_data={22'b0,done1,done0,6'b0,busy1,busy0};
            32'h030: rd_data={16'b0,error0,result0};
            32'h034: rd_data={16'b0,error1,result1};
            32'h038: rd_data={25'b0,io_sync};
            32'h03c: rd_data={16'b0,data_sync}; // asynchronous pin levels, NOT a captured pixel
            32'h100: rd_data=snap0[0]; 32'h104: rd_data=snap0[1];
            32'h108: rd_data=snap0[2]; 32'h10c: rd_data=snap0[3];
            32'h110: rd_data=snap0[4]; 32'h114: rd_data=snap0[5]; 32'h118: rd_data=snap0[6];
            32'h140: rd_data=snap1[0]; 32'h144: rd_data=snap1[1];
            32'h148: rd_data=snap1[2]; 32'h14c: rd_data=snap1[3];
            32'h150: rd_data=snap1[4]; 32'h154: rd_data=snap1[5]; 32'h158: rd_data=snap1[6];
            default: begin rd_data=32'hdeadc0de; rd_resp=2'b11; end
        endcase
        if (rd_addr[1:0]!=0) rd_resp=2'b10;
    end
    always @(posedge clk) begin
        if (!resetn) begin
            scratch<=0; ticks<=0; snap_ticks<=0; snap_generation<=0; io_control<=0;
            sensor_addr<=0; sensor_wdata<=0; sensor_start<=0; sensor_read<=0; sensor_recover<=0;
            io_meta<=0; io_sync<=0; data_meta<=0; data_sync<=0;
            for(i=0;i<7;i=i+1) begin snap0[i]<=0; snap1[i]<=0; end
        end else begin
            ticks<=ticks+1'b1; sensor_start<=0;
            io_meta<={camera_xclk_level,camera_pwdn_level,camera_reset_level,sda1,scl1,sda0,scl0};
            io_sync<=io_meta; data_meta<={data1,data0}; data_sync<=data_meta;
            if (wr_en && wr_resp==0) case(wr_addr)
                32'h00c: scratch<=(scratch&~byte_mask)|command;
                32'h014: if(wr_strb[0]) io_control<=wr_data[1:0];
                32'h018: if(command[0]) begin
                    snap_ticks<=ticks; snap_generation<=snap_generation+1'b1;
                    snap0[0]<=f0; snap0[1]<=t0; snap0[2]<=period0; snap0[3]<=lines0;
                    snap0[4]<=current0; snap0[5]<=pc0; snap0[6]<={30'b0,level0};
                    snap1[0]<=f1; snap1[1]<=t1; snap1[2]<=period1; snap1[3]<=lines1;
                    snap1[4]<=current1; snap1[5]<=pc1; snap1[6]<={30'b0,level1};
                end
                32'h020: sensor_addr<=(sensor_addr&~byte_mask[15:0])|command[15:0];
                32'h024: if(wr_strb[0]) sensor_wdata<=wr_data[7:0];
                32'h028: begin sensor_start<=command[1:0]; sensor_read<=command[8]; sensor_recover<=command[9]; end
            endcase
        end
    end
endmodule
