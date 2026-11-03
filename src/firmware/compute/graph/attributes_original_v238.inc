static void g_attributes_original_v238(const float *coarse,const float *native,const float *gray,const float *right_gray,float *dst,const r14_linear_axis *ax,const r14_linear_axis *ay,int first,int last){
    int at_cell,y,x,ky,kx,c;
    for(at_cell=first;at_cell<last;at_cell++){
        y=at_cell/160;x=at_cell%160;
        float fields[4][8];int sample=0;
        for(ky=0;ky<2;ky++)for(kx=0;kx<2;kx++){
            int yy=2*y+ky,xx=2*x+kx,at=yy*W+xx;
            float base=coarse[(yy/4)*80+xx/4],coord=(float)xx-base,lo_float=r14_floor_v169(coord),fraction=coord-lo_float;
            int lo=clip((int)lo_float,W),hi=clip(lo+1,W);
            float warped=right_gray[yy*W+lo]*(1-fraction)+right_gray[yy*W+hi]*fraction;
            float *v=fields[sample++];
            v[0]=base/64;v[1]=native[at];v[2]=bilinear_cached(coarse+CN,80,ay+yy,ax+xx);
            v[3]=native[N+at];v[4]=gray[at];
            v[5]=xx+1<W?gray[at+1]-gray[at]:0;
            v[6]=yy+1<H?gray[at+W]-gray[at]:0;
            v[7]=fabsf(gray[at]-warped)*(coord>=0?1.0f:0.0f);
        }
        for(c=0;c<8;c++){
            float sum=fields[0][c];sum+=fields[1][c];sum+=fields[2][c];sum+=fields[3][c];
            dst[(c*128+y)*160+x]=sum*.25f;
        }
    }
}
