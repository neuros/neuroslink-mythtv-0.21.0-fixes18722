#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "multiplex.h"
#include "ts.h"

static int buffers_filled(multiplex_t *mx)
{
	int vavail=0, aavail=0, i;

	vavail = ring_avail(mx->index_vrbuffer)/sizeof(index_unit);
	
	for (i=0; i<mx->extcnt;i++){
		aavail += ring_avail(&mx->index_extrbuffer[i])/
				     sizeof(index_unit);
	}

	if (aavail+vavail) return ((aavail+vavail));
	return 0;
}

static int use_video(uint64_t vpts, extdata_t *ext, int *aok, int n)
{
	int i;
	for(i=0; i < n; i++)
		if(aok[i] && ptscmp(vpts,ext[i].pts) > 0)
			return 0;
	return 1;
}
static int which_ext(extdata_t *ext, int *aok, int n)
{
	int i;
	int started = 0;
	int pos = -1;
	uint64_t tmppts;
	for(i=0; i < n; i++)
		if(aok[i]){
			if(! started){
				started=1;
				tmppts=ext[i].pts;
				pos = i;
			} else if(ptscmp(tmppts, ext[i].pts) > 0) {
				tmppts = ext[i].pts;
				pos = i;
			}
		}
	return pos;
}

static int peek_next_video_unit(multiplex_t *mx, index_unit *viu)
{
	if (!ring_avail(mx->index_vrbuffer) && mx->finish) return 0;

	while (ring_avail(mx->index_vrbuffer) < sizeof(index_unit))
		if (mx->fill_buffers(mx->priv, mx->finish)< 0) {
			fprintf(stderr,"error in peek next video unit\n");
			return 0;
		}

	ring_peek(mx->index_vrbuffer, (uint8_t *)viu, sizeof(index_unit),0);
#ifdef OUT_DEBUG
	fprintf(stderr,"video index start: %d  stop: %d  (%d)  rpos: %d\n", 
		viu->start, (viu->start+viu->length),
		viu->length, ring_rpos(mx->vrbuffer));
#endif

	return 1;
}
	
static int get_next_video_unit(multiplex_t *mx, index_unit *viu)
{
	index_unit nviu;
	if (!ring_avail(mx->index_vrbuffer) && mx->finish) return 0;

	while (ring_avail(mx->index_vrbuffer) < sizeof(index_unit))
		if (mx->fill_buffers(mx->priv, mx->finish)< 0) {
			fprintf(stderr,"error in get next video unit\n");
			return 0;
		}

	ring_read(mx->index_vrbuffer, (uint8_t *)viu, sizeof(index_unit));
#ifdef OUT_DEBUG
	fprintf(stderr,"video index start: %d  stop: %d  (%d)  rpos: %d\n", 
		viu->start, (viu->start+viu->length),
		viu->length, ring_rpos(mx->vrbuffer));
#endif
	if(! peek_next_video_unit(mx, &nviu))
		return 1;
	//left-shift by 8 to increase precision
	viu->ptsrate = (uptsdiff(nviu.dts, viu->dts) << 8) / viu->length;
	return 1;
}

static int peek_next_ext_unit(multiplex_t *mx, index_unit *extiu, int i)
{
	if (!ring_avail(&mx->index_extrbuffer[i]) && mx->finish) return 0;

	while (ring_avail(&mx->index_extrbuffer[i]) < sizeof(index_unit))
		if (mx->fill_buffers(mx->priv, mx->finish)< 0) {
			fprintf(stderr,"error in peek next video unit\n");
			return 0;
		}

	ring_peek(&mx->index_extrbuffer[i], (uint8_t *)extiu,
		  sizeof(index_unit),0);
#ifdef OUT_DEBUG
	fprintf(stderr,"ext index start: %d  stop: %d  (%d)  rpos: %d\n", 
		extiu->start, (extiu->start+extiu->length),
		extiu->length, ring_rpos(mx->extrbuffer));
#endif

	return 1;
}
	
static int get_next_ext_unit(multiplex_t *mx, index_unit *extiu, int i)
{
	index_unit niu, *piu = extiu;
	int j, length = 0;
	for(j = 0; j < mx->ext[i].frmperpkt; j++) {
		if (!ring_avail(&mx->index_extrbuffer[i]) && mx->finish)
			break;

		while(ring_avail(&mx->index_extrbuffer[i]) < sizeof(index_unit))
			if (mx->fill_buffers(mx->priv, mx->finish)< 0) {
				fprintf(stderr,"error in get next ext unit\n");
				break;
			}
	
		ring_read(&mx->index_extrbuffer[i], (uint8_t *)piu,
			  sizeof(index_unit));
		length += piu->length;
		piu = &niu;
	}
	if (j == 0)
		return 0;
	extiu->length = length;
	extiu->framesize = length;
	if(! peek_next_ext_unit(mx, &niu, i))
		return 1;
	//left-shift by 8 to increase precision
	extiu->ptsrate = (uptsdiff(niu.pts, extiu->pts) << 8) / extiu->length;

#ifdef OUT_DEBUG
	fprintf(stderr,"ext index start: %d  stop: %d  (%d)  rpos: %d\n", 
		extiu->start, (extiu->start+extiu->length),
		extiu->length, ring_rpos(&mx->extrbuffer[i]));
#endif
	return 1;
}

static uint8_t get_ptsdts(multiplex_t *mx, index_unit *viu)
{
	uint8_t ptsdts = 0;
	switch (mx->frame_timestamps){
	case TIME_ALWAYS:
		if (viu->frame == I_FRAME || viu->frame == P_FRAME)
			ptsdts = PTS_DTS;
		else 
			ptsdts = PTS_ONLY;
		break;

	case TIME_IFRAME:
		if (viu->frame == I_FRAME)
			ptsdts = PTS_DTS;
		break;
	}
	return ptsdts;
}

static void writeout_video(multiplex_t *mx)
{  
	uint8_t outbuf[3000];
	int written=0;
	uint8_t ptsdts=0;
	unsigned int length;
	int nlength=0;
        int frame_len=0;
	index_unit *viu = &mx->viu;

#ifdef OUT_DEBUG
	fprintf(stderr,"writing VIDEO pack\n");
#endif

	if(viu->frame_start) {
		ptsdts = get_ptsdts(mx, viu);
                frame_len = viu->length;
	}

	if (viu->frame_start && viu->seq_header && viu->gop && 
	    viu->frame == I_FRAME){
		if (!mx->startup && mx->is_ts){
			write_ts_patpmt(mx->ext, mx->extcnt, 1, outbuf);
			write(mx->fd_out, outbuf, mx->pack_size*2);
			ptsinc(&mx->SCR, mx->SCRinc*2);
		} else if (!mx->startup && mx->navpack){
			write_nav_pack(mx->pack_size, mx->extcnt, 
				       mx->SCR, mx->muxr, outbuf);
			write(mx->fd_out, outbuf, mx->pack_size);
			ptsinc(&mx->SCR, mx->SCRinc);
		} else mx->startup = 0;
#ifdef OUT_DEBUG
		fprintf (stderr, " with sequence and gop header\n");
#endif
	}

	if (mx->finish != 2 && dummy_space(&mx->vdbuf) < mx->data_size){
		return;
	}
	length = viu->length;
	while (!mx->is_ts && length  < mx->data_size){
		index_unit nviu;
                int old_start = viu->frame_start;
                int old_frame = viu->frame;
                uint64_t old_pts = viu->pts;
                uint64_t old_dts = viu->dts;
		dummy_add(&mx->vdbuf, uptsdiff(viu->dts+mx->video_delay,0)
			  , viu->length);
		if ( peek_next_video_unit(mx, &nviu)){
			if (!(nviu.seq_header && nviu.gop && 
			      nviu.frame == I_FRAME)){
				get_next_video_unit(mx, viu);
                                frame_len = viu->length;
				length += viu->length; 
				if(old_start) {
					viu->pts = old_pts;
					viu->dts = old_dts;
					viu->frame = old_frame;
				} else {
					ptsdts = get_ptsdts(mx, viu);
				}
			} else break;
		} else break;
	}

	if (viu->frame_start){
		viu->frame_start=0;
	if (viu->gop){
                uint8_t gop[8];
                frame_len=length-frame_len;
                ring_peek(mx->vrbuffer, gop, 8, frame_len);
		pts2time( viu->pts + mx->video_delay, gop, 8);
                ring_poke(mx->vrbuffer, gop, 8, frame_len);
                viu->gop=0;
	}
		if (mx->VBR) {
			mx->extra_clock = ptsdiff(viu->dts + mx->video_delay, 
						  mx->SCR + 500*CLOCK_MS);
#ifdef OUT_DEBUG1
			fprintf(stderr,"EXTRACLOCK2: %lli %lli %lli\n", viu->dts, mx->video_delay, mx->SCR);
			fprintf(stderr,"EXTRACLOCK2: %lli ", mx->extra_clock);
			printpts(mx->extra_clock);
			fprintf(stderr,"\n");
#endif

			if (mx->extra_clock < 0)
				mx->extra_clock = 0.0;
		}
	}


	nlength = length;
	if (mx->is_ts)
		written = write_video_ts(  viu->pts+mx->video_delay, 
					   viu->dts+mx->video_delay, 
					   mx->SCR, outbuf, &nlength,
					   ptsdts, mx->vrbuffer);
	else
		written = write_video_pes( mx->pack_size, mx->extcnt, 
					   viu->pts+mx->video_delay, 
					   viu->dts+mx->video_delay, 
					   mx->SCR, mx->muxr, outbuf, &nlength,
					   ptsdts, mx->vrbuffer);

	length -= nlength;
	dummy_add(&mx->vdbuf, uptsdiff( viu->dts+mx->video_delay,0)
		  , viu->length-length);
	viu->length = length;

	//estimate next pts based on bitrate of this stream and data written
	viu->dts = uptsdiff(viu->dts + ((nlength*viu->ptsrate)>>8), 0);

	write(mx->fd_out, outbuf, written);

#ifdef OUT_DEBUG
	fprintf(stderr,"VPTS");
	printpts(viu->pts);
	fprintf(stderr," DTS");
	printpts(viu->dts);
	printpts(mx->video_delay);
	fprintf(stderr,"\n");
#endif
	
	if (viu->length == 0){
		get_next_video_unit(mx, viu);
	}

}

static void writeout_ext(multiplex_t *mx, int n)
{  
	uint8_t outbuf[3000];
	int written=0;
	unsigned int length=0;
	int nlength=0;
	uint64_t pts, dpts=0;
	int newpts=0;
	int nframes=1;
	int ac3_off=0;
	int rest_data = 5;

	int type = mx->ext[n].type;
	ringbuffer *airbuffer = &mx->index_extrbuffer[n];
	dummy_buffer *dbuf = &mx->ext[n].dbuf;
	uint64_t adelay = mx->ext[n].pts_off;
	uint64_t *apts = &mx->ext[n].pts;
	index_unit *aiu = &mx->ext[n].iu;

	switch (type){

	case MPEG_AUDIO:
#ifdef OUT_DEBUG
		fprintf(stderr,"writing AUDIO%d pack\n",n);
#endif
		break;

	case AC3:
#ifdef OUT_DEBUG
		fprintf(stderr,"writing AC3%d pack\n",n);
#endif
		rest_data = 1; // 4 bytes AC3 header
		break;

	default:
		return;
	}
	
	if (mx->finish != 2 && dummy_space(dbuf) < mx->data_size + rest_data){
		return;
	}

	pts = uptsdiff( aiu->pts + mx->audio_delay, adelay );
	*apts = pts;
	length = aiu->length;
	if (length < aiu->framesize){
		newpts = 1;
		ac3_off = length;
	}
	dummy_add(dbuf, pts, aiu->length);

#ifdef OUT_DEBUG
	fprintf(stderr,"start: %d  stop: %d (%d)  length %d ", 
		aiu->start, (aiu->start+aiu->length),
		aiu->length, length);
	printpts(*apts);
	printpts(aiu->pts);
	printpts(mx->audio_delay);
	printpts(adelay);
	printpts(pts);
	fprintf(stderr,"\n");
#endif
	while (!mx->is_ts && length  < mx->data_size + rest_data){
		if (ring_read(airbuffer, (uint8_t *)aiu, sizeof(index_unit)) > 0){
			dpts = uptsdiff(aiu->pts +mx->audio_delay, adelay );
			
			if (newpts){
				pts = dpts;
				newpts=0;
			}

			length+= aiu->length;
			if (length < mx->data_size + rest_data)
				dummy_add(dbuf, dpts, aiu->length);
			
			*apts = dpts;
			nframes++;
#ifdef OUT_DEBUG
			fprintf(stderr,"start: %d  stop: %d (%d)  length %d ", 
				aiu->start, (aiu->start+aiu->length),
				aiu->length, length);
			printpts(*apts);
			printpts(aiu->pts);
			printpts(mx->audio_delay);
			printpts(adelay);
			fprintf(stderr,"\n");
#endif
		} else if (mx->finish){
			break;
		} else if (mx->fill_buffers(mx->priv, mx->finish)< 0) {
			fprintf(stderr,"error in writeout ext\n");
			exit(1);
		}
	}

	nlength = length;

	switch (type) {
	case MPEG_AUDIO:
		if(mx->is_ts)
			written = write_audio_ts( mx->ext[n].strmnum, pts,
					outbuf, &nlength, newpts ? 0 : PTS_ONLY,
					&mx->extrbuffer[n]);
		else
			written = write_audio_pes( mx->pack_size, mx->extcnt,
					mx->ext[n].strmnum, pts, mx->SCR,
					mx->muxr, outbuf, &nlength, PTS_ONLY,
					&mx->extrbuffer[n]);
		break;
	case AC3:
		if(mx->is_ts)
			written = write_ac3_ts(mx->ext[n].strmnum, pts,
					outbuf, &nlength, newpts ? 0 : PTS_ONLY,
					mx->ext[n].frmperpkt, &mx->extrbuffer[n]);
		else
			written = write_ac3_pes( mx->pack_size, mx->extcnt,
					mx->ext[n].strmnum, pts, mx->SCR,
					mx->muxr, outbuf, &nlength, PTS_ONLY,
					nframes, ac3_off,
					&mx->extrbuffer[n]);
		break;
	}

	length -= nlength;
	write(mx->fd_out, outbuf, written);

	dummy_add(dbuf, dpts, aiu->length-length);
	aiu->length = length;
	aiu->start = ring_rpos(&mx->extrbuffer[n]);

	if (aiu->length == 0){
		get_next_ext_unit(mx, aiu, n);
	} else {
		//estimate next pts based on bitrate of stream and data written
		aiu->pts = uptsdiff(aiu->pts + ((nlength*aiu->ptsrate)>>8), 0);
	}
	*apts = uptsdiff(aiu->pts + mx->audio_delay, adelay);
#ifdef OUT_DEBUG
	if ((int64_t)*apts < 0) fprintf(stderr,"SCHEISS ");
	fprintf(stderr,"APTS");
	printpts(*apts);
	printpts(aiu->pts);
	printpts(mx->audio_delay);
	printpts(adelay);
	fprintf(stderr,"\n");
#endif

	if (mx->fill_buffers(mx->priv, mx->finish)< 0) {
		fprintf(stderr,"error in writeout ext\n");
		exit(1);
	}
}

static void writeout_padding (multiplex_t *mx)
{
	uint8_t outbuf[3000];
	//fprintf(stderr,"writing PADDING pack\n");

	write_padding_pes( mx->pack_size, mx->extcnt, mx->SCR, 
			   mx->muxr, outbuf);
	write(mx->fd_out, outbuf, mx->pack_size);
}

void check_times( multiplex_t *mx, int *video_ok, int *ext_ok, int *start)
{
	int i;
	int set_ok = 0;

	memset(ext_ok, 0, N_AUDIO*sizeof(int));
	*video_ok = 0;
	
	if (mx->fill_buffers(mx->priv, mx->finish)< 0) {
		fprintf(stderr,"error in get next video unit\n");
		return;
	}

	/* increase SCR to next packet */
	mx->oldSCR = mx->SCR;
	if (!*start){ 
		ptsinc(&mx->SCR, mx->SCRinc);
	} else *start = 0;
	
	if (mx->VBR) {
#ifdef OUT_DEBUG1
		fprintf(stderr,"EXTRACLOCK: %lli ", mx->extra_clock);
		printpts(mx->extra_clock);
		fprintf(stderr,"\n");
#endif
		
		if (mx->extra_clock > 0.0) {
			int64_t d = mx->extra_clock/ mx->SCRinc - 1;
			if (d > 0)
				mx->extra_clock = d*mx->SCRinc;
			else
				mx->extra_clock = 0.0;
		}
		
		if (mx->extra_clock > 0.0) {
			int64_t temp_scr = mx->extra_clock;
			
			for (i=0; i<mx->extcnt; i++){
				if (ptscmp(mx->SCR + temp_scr + 100*CLOCK_MS, 
					   mx->ext[i].iu.pts) > 0) {
					while (ptscmp(mx->SCR + temp_scr 
						      + 100*CLOCK_MS,
						      mx->ext[i].iu.pts) > 0) 
						temp_scr -= mx->SCRinc;
					temp_scr += mx->SCRinc;
				}
			}
			
			if (temp_scr > 0.0) {
				mx->SCR += temp_scr;
				mx->extra_clock -= temp_scr;
			} else
				mx->extra_clock = 0.0;
		}
	}
	
	/* clear decoder buffers up to SCR */
	dummy_delete(&mx->vdbuf, mx->SCR);    
	
	for (i=0;i <mx->extcnt; i++)
		dummy_delete(&mx->ext[i].dbuf, mx->SCR);
	
	if (dummy_space(&mx->vdbuf) > mx->vsize && mx->viu.length > 0 &&
	    (ptscmp(mx->viu.dts + mx->video_delay, 500*CLOCK_MS +mx->oldSCR)<0)
	    && ring_avail(mx->index_vrbuffer)){
		*video_ok = 1;
                set_ok = 1;
	}
	
	for (i = 0; i < mx->extcnt; i++){
		if (dummy_space(&mx->ext[i].dbuf) > mx->extsize && 
		    mx->ext[i].iu.length > 0 &&
		    ptscmp(mx->ext[i].pts, 500*CLOCK_MS + mx->oldSCR) < 0
		    && ring_avail(&mx->index_extrbuffer[i])){
			ext_ok[i] = 1;
                        set_ok = 1;
		}
	}
#ifdef OUT_DEBUG
	if (set_ok) {
		fprintf(stderr, "SCR");
		printpts(mx->oldSCR);
		fprintf(stderr, "VDTS");
		printpts(mx->viu.dts);
		fprintf(stderr, " (%d)", *video_ok);
		fprintf(stderr, " EXT");
		for (i = 0; i < mx->extcnt; i++){
			fprintf(stderr, "%d:", mx->ext[i].type);
			printpts(mx->ext[i].pts);
			fprintf(stderr, " (%d)", ext_ok[i]);
		}
		fprintf(stderr, "\n");
	}
#endif
}
void write_out_packs( multiplex_t *mx, int video_ok, int *ext_ok)
{
	int i;

	if (video_ok && use_video(mx->viu.dts + mx->video_delay,
	    mx->ext, ext_ok, mx->extcnt)) {
		writeout_video(mx);  
	} else { // second case(s): audio ok, video in time
		i = which_ext(mx->ext, ext_ok, mx->extcnt);
		int done=0;
		if(i>=0) {
			writeout_ext(mx, i);
			done = 1;
		}
		if (!done && !mx->VBR){
			writeout_padding(mx);
		}
	}
	
}

void finish_mpg(multiplex_t *mx)
{
	int start=0;
	int video_ok = 0;
	int ext_ok[N_AUDIO];
        int n,nn,old,i;
        uint8_t mpeg_end[4] = { 0x00, 0x00, 0x01, 0xB9 };
                                                                                
        memset(ext_ok, 0, N_AUDIO*sizeof(int));
        mx->finish = 1;
                                                                                
        old = 0;nn=0;
        while ((n=buffers_filled(mx)) && nn<1000 ){
                if (n== old) nn++;
                else  nn=0;
                old = n;
                check_times( mx, &video_ok, ext_ok, &start);
                write_out_packs( mx, video_ok, ext_ok);
        }

        old = 0;nn=0;
	while ((n=ring_avail(mx->index_vrbuffer)/sizeof(index_unit))
	       && nn<1000){
		if (n== old) nn++;
		else nn= 0;
		old = n;
		writeout_video(mx);  
	}
	
// flush the rest
        mx->finish = 2;
        old = 0;nn=0;
	for (i = 0; i < mx->extcnt; i++){
		while ((n=ring_avail(&mx->index_extrbuffer[i])/
				     sizeof(index_unit)) && nn <100){
			if (n== old) nn++;
			else nn = 0;
			old = n;
			writeout_ext(mx, i);
		}
	}
	
	if (mx->otype == REPLEX_MPEG2)
		write(mx->fd_out, mpeg_end,4);

	dummy_destroy(&mx->vdbuf);
	for (i=0; i<mx->extcnt;i++)
		dummy_destroy(&mx->ext[i].dbuf);
}

static int get_ts_video_overhead(int pktsize, sequence_t *seq)
{
	uint32_t framesize;
	uint32_t numpkt;
	int pktdata = pktsize - TS_HEADER_MIN;
	framesize = seq->bit_rate * 50 / seq->frame_rate; //avg bytes/frame
	numpkt = (framesize + PES_H_MIN + 10 + pktdata -1) / pktdata;
	return pktsize- ((pktsize * numpkt) - framesize + numpkt - 1) / numpkt;
}

static int get_ts_ext_overhead(int pktsize, audio_frame_t *extframe,
				extdata_t *ext, int cnt)
{
	int i, max = 0;
	int pktdata = pktsize - TS_HEADER_MIN;
	for (i = 0; i < cnt; i++) {
		int size, numpkt, overhead;
		// 1/53 is approx 0.15 * 1/8 which allows us to calculate the
		// # of packets in .15 seconds (which is the number of packets
		// per PES.
		ext[i].frmperpkt = extframe[i].bit_rate / 53 /
					extframe[i].framesize;
		size = extframe[i].framesize * ext[i].frmperpkt;
		numpkt = (size + pktdata - 1) / pktdata;
		overhead = (pktsize * numpkt - size + numpkt - 1) / numpkt;
		if(overhead > max)
			max = overhead;
	}
	return pktsize - max;
}

void init_multiplex( multiplex_t *mx, sequence_t *seq_head,
                     audio_frame_t *extframe, int *exttype, int *exttypcnt,
		     uint64_t video_delay, uint64_t audio_delay, int fd,
		     int (*fill_buffers)(void *p, int f),
		     ringbuffer *vrbuffer, ringbuffer *index_vrbuffer,	
		     ringbuffer *extrbuffer, ringbuffer *index_extrbuffer,
		     int otype)
{
	int i;
	uint32_t data_rate;

	mx->fill_buffers = fill_buffers;
	mx->video_delay = video_delay;
	mx->audio_delay = audio_delay;
	mx->fd_out = fd;
	mx->otype = otype;

	switch(mx->otype){

	case REPLEX_DVD:
		mx->video_delay += 180*CLOCK_MS;
		mx->audio_delay += 180*CLOCK_MS;
		mx->pack_size = 2048;
		mx->audio_buffer_size = 4*1024;
		mx->video_buffer_size = 232*1024;
		mx->mux_rate = 1260000;
		mx->navpack = 1;
		mx->frame_timestamps = TIME_IFRAME;
		mx->VBR = 1;
		mx->reset_clocks = 0;
		mx->write_end_codes = 0;
		mx->set_broken_link = 0;
		mx->is_ts = 0;
		break;


	case REPLEX_MPEG2:
		mx->video_delay += 180*CLOCK_MS;
		mx->audio_delay += 180*CLOCK_MS;
		mx->pack_size = 2048;
		mx->audio_buffer_size = 4*1024;
		mx->video_buffer_size = 224*1024;
		mx->mux_rate = 0;
		mx->navpack = 0;
		mx->frame_timestamps = TIME_ALWAYS;
		mx->VBR = 1;
		mx->reset_clocks = 1;
		mx->write_end_codes = 1;
		mx->set_broken_link = 1;
		mx->is_ts = 0;
		break;

	case REPLEX_HDTV:
		mx->video_delay += 180*CLOCK_MS;
		mx->audio_delay += 180*CLOCK_MS;
		mx->pack_size = 2048;
		mx->audio_buffer_size = 4*1024;
		mx->video_buffer_size = 4*224*1024;
		mx->mux_rate = 0;
		mx->navpack = 0;
		mx->frame_timestamps = TIME_ALWAYS;
		mx->VBR = 1;
		mx->reset_clocks = 1;
		mx->write_end_codes = 1;
		mx->set_broken_link = 1;
		mx->is_ts = 0;
		break;

	case REPLEX_TS_SD:
		mx->video_delay += 180*CLOCK_MS;
		mx->audio_delay += 180*CLOCK_MS;
		mx->pack_size = 188;
		mx->audio_buffer_size = 4*1024;
		mx->video_buffer_size = 232*1024;
		mx->mux_rate = 1260000;
		mx->navpack = 0;
		mx->frame_timestamps = TIME_ALWAYS;
		mx->VBR = 1;
		mx->reset_clocks = 0;
		mx->write_end_codes = 0;
		mx->set_broken_link = 0;
		mx->is_ts = 1;
		break;

	case REPLEX_TS_HD:
		mx->video_delay += 180*CLOCK_MS;
		mx->audio_delay += 180*CLOCK_MS;
		mx->pack_size = 188;
		mx->audio_buffer_size = 4*1024;
		mx->video_buffer_size = 4*224*1024;
		mx->mux_rate = 0;
		mx->navpack = 0;
		mx->frame_timestamps = TIME_ALWAYS;
		mx->VBR = 1;
		mx->reset_clocks = 0;
		mx->write_end_codes = 0;
		mx->set_broken_link = 0;
		mx->is_ts = 1;
		break;
	}

	for (mx->extcnt = 0, data_rate = 0, i = 0;
			 i < N_AUDIO && exttype[i]; i++){
		if (exttype[i] >= MAX_TYPES) {
			fprintf(stderr, "Found illegal stream type %d\n",
				exttype[i]);
			exit(1);
		}
		mx->ext[i].type = exttype[i];
		mx->ext[i].pts_off = 0;
		mx->ext[i].frmperpkt = 1;
		mx->ext[i].strmnum = exttypcnt[i];
		strncpy(mx->ext[i].language, extframe[i].language, 4);
		dummy_init(&mx->ext[i].dbuf, mx->audio_buffer_size);
		data_rate += extframe[i].bit_rate;
		mx->extcnt++;
	}

	mx->vrbuffer = vrbuffer;
	mx->index_vrbuffer = index_vrbuffer;
	mx->extrbuffer = extrbuffer;
	mx->index_extrbuffer = index_extrbuffer;

	dummy_init(&mx->vdbuf, mx->video_buffer_size);

	//NOTE: vpkt_hdr/extpkt_hdr are worst-case for PS streams, but
	//best-guess for TS streams
	if(mx->is_ts) {
		//Use best guess for TS streams
		mx->data_size = get_ts_video_overhead(mx->pack_size, seq_head);
		mx->extsize = get_ts_ext_overhead(mx->pack_size, extframe,
				mx->ext, mx->extcnt);
		
	} else {
		//Use worst case for PS streams
		mx->data_size = mx->pack_size - PES_H_MIN - PS_HEADER_L1 - 10;
		mx->extsize = mx->data_size + 5; //one less DTS
	}
	mx->vsize = mx->data_size;
	
	data_rate += seq_head->bit_rate *400;

	mx->muxr = ((uint64_t)data_rate / 8 * mx->pack_size) / mx->data_size; 
                                     // muxrate of payload in Byte/s

	if (mx->mux_rate) {
		if ( mx->mux_rate < mx->muxr)
                        fprintf(stderr, "data rate may be to high for required mux rate\n");
                mx->muxr = mx->mux_rate;
        }
	fprintf(stderr, "Mux rate: %.2f Mbit/s\n", mx->muxr*8.0/1000000.);
	
	mx->SCRinc = 27000000ULL/((uint64_t)mx->muxr / 
				     (uint64_t) mx->pack_size);
	
}

void setup_multiplex(multiplex_t *mx)
{
	int i;

 	get_next_video_unit(mx, &mx->viu);
	for (i=0; i < mx->extcnt; i++)
        {
		get_next_ext_unit(mx, &mx->ext[i].iu, i);
		if (mx->ext[i].type == MPEG_AUDIO || mx->ext[i].type == AC3)
			mx->ext[i].pts = uptsdiff(
					mx->ext[i].iu.pts + mx->audio_delay, 
					mx->ext[i].pts_off);
		else
			mx->ext[i].pts = uptsdiff(
					mx->ext[i].iu.pts, mx->ext[i].pts_off);
	}

	mx->SCR = 0;

	// write first VOBU header
	if (mx->is_ts) {
		uint8_t outbuf[2048];
		write_ts_patpmt(mx->ext, mx->extcnt, 1, outbuf);
		write(mx->fd_out, outbuf, mx->pack_size*2);
		ptsinc(&mx->SCR, mx->SCRinc*2);
		mx->startup = 1;
	} else if (mx->navpack){
		uint8_t outbuf[2048];
		write_nav_pack(mx->pack_size, mx->extcnt, 
			       mx->SCR, mx->muxr, outbuf);
		write(mx->fd_out, outbuf, mx->pack_size);
		ptsinc(&mx->SCR, mx->SCRinc);
		mx->startup = 1;
	} else mx->startup = 0;
}
