
#include <alsa/asoundlib.h>
#include <alsa/pcm_external.h>

#include <unistd.h>

static unsigned int sfmt[] = {	SND_PCM_FORMAT_S16_LE, \
								SND_PCM_FORMAT_S24_LE, \
								SND_PCM_FORMAT_S24_3LE, \
								SND_PCM_FORMAT_DSD_U32_LE, \
							};
							

typedef struct {
	snd_pcm_extplug_t ext;
} s2mono_info_t;


static inline void *area_addr(const snd_pcm_channel_area_t *area, snd_pcm_uframes_t offset)
{
	unsigned int bitofs = area->first + area->step * offset;
	return (char *) area->addr + bitofs / 8;
}

static snd_pcm_sframes_t s2m_dsd_transfer(void *src_area_ptr, void *dst_area_ptr, snd_pcm_uframes_t size)
{
	uint16_t *src_ptr = src_area_ptr;
	uint16_t *dst_ptr = dst_area_ptr;
	snd_pcm_uframes_t count = 2 * size;
	do {
		// word swap
		*dst_ptr = *(src_ptr + 1);
		*(dst_ptr + 1) = *src_ptr;

		src_ptr += 2;
		dst_ptr += 2;
	} while(--count);
	return size;
}

static snd_pcm_sframes_t s2m_24to32_transfer(void *src_area_ptr, void *dst_area_ptr, snd_pcm_uframes_t size)
{
	uint8_t *src_ptr = src_area_ptr;	// frame size 6
	uint8_t *dst_ptr = dst_area_ptr;	// frame size 8
	snd_pcm_uframes_t count = 2 * size;
	do {
		*dst_ptr = *src_ptr;
		*(dst_ptr + 1) = *(src_ptr + 1);
		*(dst_ptr + 2) = *(src_ptr + 2);
		*(dst_ptr + 3) = 0;

		src_ptr += 3;
		dst_ptr += 4;
	} while(--count);
	return size;
}

static snd_pcm_sframes_t s2m_transfer(snd_pcm_extplug_t *ext,
	     const snd_pcm_channel_area_t *dst_areas,
	     snd_pcm_uframes_t dst_offset,
	     const snd_pcm_channel_area_t *src_areas,
	     snd_pcm_uframes_t src_offset,
	     snd_pcm_uframes_t size)
{
	void *src_area_ptr = area_addr(src_areas, src_offset);
	void *dst_area_ptr = area_addr(dst_areas, dst_offset);
	
	// s16 copy
	if( src_areas->step == 32 && dst_areas->step == 32 && ext->format == SND_PCM_FORMAT_S16_LE ) {
		memcpy(dst_area_ptr, src_area_ptr, size * 4);
		return size;
	}
	// s24 copy
	if( src_areas->step == 64 && dst_areas->step == 64 && ext->format == SND_PCM_FORMAT_S24_LE ) {
		memcpy(dst_area_ptr, src_area_ptr, size * 8);
		return size;
	}
	// s24_3 copy
	if( src_areas->step == 48 && dst_areas->step == 48 && ext->format == SND_PCM_FORMAT_S24_3LE ) {
		memcpy(dst_area_ptr, src_area_ptr, size * 6);
		return size;
	}
	// DSD word reorder
	if( src_areas->step == 64 && dst_areas->step == 64 && ext->format == SND_PCM_FORMAT_DSD_U32_LE ) 
		return s2m_dsd_transfer(src_area_ptr, dst_area_ptr, size);
	// SND_PCM_FORMAT_S24_3LE to SND_PCM_FORMAT_S24 convert
	if( src_areas->step == 48 && dst_areas->step == 64 && ext->format == SND_PCM_FORMAT_S24_3LE ) 
		return s2m_24to32_transfer(src_area_ptr, dst_area_ptr, size);

	SNDERR( "Unsupported format=%d or steps: src_step=%u, dst_step=%u\n", 
		(int)ext->format, src_areas->step, dst_areas->step);
	return -EINVAL;
}

//static int s2m_hw_params(snd_pcm_extplug_t *ext, snd_pcm_hw_params_t *params)
//{
//	snd_pcm_format_t format;
//	int err = snd_pcm_hw_params_get_format(params, &format);
//	printf("err=%d  format=%u\n", err, format);
//	return 0;
//}

static void s2m_dump(snd_pcm_extplug_t *ext ATTRIBUTE_UNUSED, snd_output_t *out)
{
	snd_output_printf(out, "ExtPlugin: s2mono\n");
}

static const snd_pcm_extplug_callback_t s2m_callback = {
    .transfer = s2m_transfer,			//snd_pcm_sframes_t(* transfer)(snd_pcm_extplug_t *ext, const snd_pcm_channel_area_t *dst_areas, snd_pcm_uframes_t dst_offset, const snd_pcm_channel_area_t *src_areas, snd_pcm_uframes_t src_offset, snd_pcm_uframes_t size);
    //.close = s2m_close,					//int(* close)(snd_pcm_extplug_t *ext);
    //.hw_params = s2m_hw_params, 		//int(* hw_params)(snd_pcm_extplug_t *ext, snd_pcm_hw_params_t *params);
    //.hw_free = s2m_hw_free, 			//int(* hw_free)(snd_pcm_extplug_t *ext);
    .dump = s2m_dump, 					//void(* dump)(snd_pcm_extplug_t *ext, snd_output_t *out);
    //.init = s2m_init,					//int(* init)(snd_pcm_extplug_t *ext);
    //.query_chmaps = s2m_query_chmaps,	//snd_pcm_chmap_query_t**(* query_chmaps)(snd_pcm_extplug_t *ext);
    //.get_chmap = s2m_get_chmap,			//snd_pcm_chmap_t*(* get_chmap)(snd_pcm_extplug_t *ext);
    //.set_chmap = s2m_set_chmap,			//int(* set_chmap)(snd_pcm_extplug_t *ext, const snd_pcm_chmap_t *map);

};

#define ARRAY_SIZE(ary)	(sizeof(ary)/sizeof(ary[0]))

//int (snd_pcm_t **pcmp, const char *name, snd_config_t *root,
//    snd_config_t *conf, snd_pcm_stream_t stream, int mode)
SND_PCM_PLUGIN_DEFINE_FUNC(s2mono)
{
	snd_config_iterator_t i, next;
	s2mono_info_t *s2m;
	snd_config_t *slave_conf = NULL;
	int err;

	s2m = calloc(1, sizeof(*s2m));
	if (!s2m)
		return -ENOMEM;

	snd_config_for_each(i, next, conf) {
		snd_config_t *n = snd_config_iterator_entry(i);
		const char *id;
		if (snd_config_get_id(n, &id) < 0)
			continue;
		if (strcmp(id, "comment") == 0 || strcmp(id, "type") == 0 ||
		    strcmp(id, "hint") == 0)
			continue;
		if (strcmp(id, "slave") == 0) {
			slave_conf = n;
			continue;
		}

		SNDERR("Unknown field %s", id);
		err = -EINVAL;
		if (err < 0) 
			goto lerr;
	}

	if (!slave_conf) {
		SNDERR("No slave configuration for s2mono pcm");
		err = -EINVAL;
		goto lerr;
	}
	s2m->ext.version = SND_PCM_EXTPLUG_VERSION;
	s2m->ext.name = "2ch to 4ch Plugin";
	s2m->ext.callback = &s2m_callback;
	s2m->ext.private_data = s2m;

	err = snd_pcm_extplug_create(&s2m->ext, name, root, slave_conf,
				     stream, mode);
	if (err < 0) {
		goto lerr;
	}

	snd_pcm_extplug_set_param_link(&s2m->ext, SND_PCM_EXTPLUG_HW_CHANNELS, 0);
	snd_pcm_extplug_set_param(&s2m->ext, SND_PCM_EXTPLUG_HW_CHANNELS, 2);
	snd_pcm_extplug_set_slave_param(&s2m->ext, SND_PCM_EXTPLUG_HW_CHANNELS, 2);

	snd_pcm_extplug_set_param_link(&s2m->ext, SND_PCM_EXTPLUG_HW_FORMAT, 1);
	snd_pcm_extplug_set_param_list( &s2m->ext, SND_PCM_EXTPLUG_HW_FORMAT, ARRAY_SIZE(sfmt), sfmt);

	//snd_pcm_extplug_set_param_link(&s2m->ext, SND_PCM_EXTPLUG_HW_FORMAT, 0);
	//snd_pcm_extplug_set_param_list( &s2m->ext, SND_PCM_EXTPLUG_HW_FORMAT, ARRAY_SIZE(sfmt), sfmt);
	//snd_pcm_extplug_set_slave_param(&s2m->ext, SND_PCM_EXTPLUG_HW_FORMAT, SND_PCM_FORMAT_S24);

	*pcmp = s2m->ext.pcm;
	return 0;

lerr:
	free(s2m);
	return err;
}
SND_PCM_PLUGIN_SYMBOL(s2mono);
