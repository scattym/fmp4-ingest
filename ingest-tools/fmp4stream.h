/*******************************************************************************
Supplementary software media ingest specification:
https://github.com/unifiedstreaming/fmp4-ingest

Copyright (C) 2009-2018 CodeShop B.V.
http://www.code-shop.com

 - parse fmp4/cmaf file for media ingest in init and media fragment
 - shows some of the functionalities, code is non-optimized, not production level code

******************************************************************************/

#ifndef fMP4Stream_H
#define fMP4Stream_H

#include <iostream>
#include <fstream>
#include <vector>
#include <stdint.h>
#include <string>
#include <bitset>
#include <limits>
#include <memory>
#include <iomanip>

using namespace std;
#define IS_BIG_ENDIAN (*(uint16_t *)"\0\xff" < 0x100)

namespace fMP4Stream {

	//----------------- structures for storing an fmp4 stream defined in ISOBMMF fMP4 ----------------------
	struct box
	{
		uint32_t size_;
		uint64_t large_size_;
		string box_type_;
		uint8_t extended_type_[16];
		vector<uint8_t> box_data_;
		bool is_large_;
		bool has_uuid_;

		box() : size_(0), large_size_(0), box_type_(""),is_large_(false), has_uuid_(false) {};
		virtual uint64_t size();
		virtual void print();
		virtual bool read(istream *istr);
		virtual void parse(char * ptr);
	
	};

	struct full_box : public box
	{
		uint8_t version_;
		unsigned int flags_;
		uint32_t magic_conf_;
		
		//
		virtual void parse(char *ptr);
		virtual void print();
		virtual uint64_t size() { return box::size() + 4; };
	};


	struct mvhd : public full_box
	{
		uint64_t creation_time_;
		uint64_t modification_time_;

		uint32_t time_scale_;
		uint64_t duration_;

		float    rate_;
		float    volume_;

		vector<uint32_t>  matrix_;
		uint32_t next_track_id_;

		void parse(char *ptr);
	};

	struct tkhd : public full_box
	{
		uint64_t creation_time_;
		uint64_t modification_time_;
		uint32_t track_id_;
		uint32_t reserved_;
		uint64_t duration_;

		uint32_t width_;
		uint32_t height_;

		uint32_t box_size_; // use the box size we want to skip
	};

	struct mfhd : public full_box
	{
		uint32_t seq_nr_;
		void parse(char * ptr);
		mfhd() : full_box() { box_type_ = string("mfhd"); };
		void print(); 
		uint64_t size() { return full_box::size() + 4; };
	};


	struct traf : public box
	{
		traf() : box() { box_type_ = string("traf"); };
	};

	struct tfhd : public full_box
	{
		bool base_data_offset_present_;
		bool sample_description_index_present_;
		bool default_sample_duration_present_;
		bool default_sample_size_present_;
		bool default_sample_flags_present_;
		bool duration_is_empty_;
		bool default_base_is_moof_;

		uint32_t track_id_;
		uint64_t base_data_offset_;
		uint32_t sample_description_index_;
		uint32_t default_sample_duration_;
		uint32_t default_sample_size_;
		uint32_t default_sample_flags_;

		tfhd() { box_type_ = string("tfhd"); };

		virtual void parse(char * ptr);
		virtual uint64_t size();
		virtual void print();
		//uint32_t size(return )
	};

	struct tfdt : public full_box
	{
		uint64_t base_media_decode_time_;
		tfdt() { box_type_ = string("tfdt"); };

		virtual uint64_t size();
		virtual void parse(char * ptr);
		virtual void print();
	};

	struct trun : public full_box
	{
		struct sample_entry
		{
			uint32_t sample_duration_;
			uint32_t sample_size_;
			uint32_t sample_flags_;
			uint32_t sample_composition_time_offset_v0_;
			int32_t  sample_composition_time_offset_v1_;
			virtual void print()
			{
				//cout << "trun sample entry: "; 
				cout << setw(33) << left << " sample duration: " << sample_duration_ << endl;
				cout << setw(33) << left << " sample size: " << sample_size_ << endl;
				//cout << "" << sample_flags << endl;
				//cout << "" << sample_composition_time_offset_v0 <<endl;
				//cout << "" << sample_composition_time_offset_v1 << endl;
			};
		};

		uint32_t sample_count_;
		// optional fields
		int32_t data_offset_;
		uint32_t first_sample_flags_;

		//flags
		bool data_offset_present_;
		bool first_sample_flags_present_;
		bool sample_duration_present_;
		bool sample_size_present_;
		bool sample_flags_present_;
		bool sample_composition_time_offsets_present_;
		//entries
		vector<sample_entry> m_sentry;
		
		//trun methods
		virtual uint64_t size();
		virtual void parse(char * ptr);
		virtual void print();
		
		trun() { this->box_type_ = "trun"; }
	};

	// other boxes in the moof box
	struct senc 
	{};

	struct saio
	{};
	
	struct saiz
	{};

	struct sbgp
	{};

	struct sgpd
	{};

	struct subs
	{};

	struct meta
	{};

	struct mdat : public box
	{
		mdat() { this->box_type_ = string("mdat"); };
		vector<uint8_t> m_bytes;
	};

	// dash emsg box
	struct emsg : public full_box
	{
		//emsg data
		string scheme_id_uri_;
		string value_;
		uint32_t timescale_;
		uint32_t presentation_time_delta_;
		uint64_t presentation_time_;
		uint32_t event_duration_;
		uint32_t id_;
		vector<uint8_t> message_data_;

		// emsg methods
		emsg() { this->box_type_ = string("emsg"); }
		virtual uint64_t size();
		virtual void parse(char * ptr, unsigned int data_size);
		virtual void print();
		uint32_t write(ostream *ostr);
		void write_emsg_as_fmp4_fragment(ostream *ostr, uint64_t timestamp, uint32_t track_id, uint64_t next_tdft);
		void write_emsg_as_mpd_event(ostream *ostr, uint64_t base_time);
	};	

	const uint8_t empty_mfra[8] = {
		0x00, 0x00, 0x00, 0x08, 'm', 'f', 'r', 'a'
	};

	const uint8_t sparse_ftyp[20]
	{
		0x00, 0x00, 0x00, 0x14, 'f', 't', 'y', 'p','c','m','f','m', 0x00,0x00,0x00,0x00,'c','m','f','c'
	};

	//-------------------------------- SCTE 35 parsing and detection -------------------
	struct sc35_splice_info
	{
		// scte 35 splice info fields
		uint8_t table_id_;
		bool section_syntax_indicator_;
		bool private_indicator_;
		uint16_t section_length_;
		uint8_t protocol_version_;
		bool encrypted_packet_;
		uint8_t encryption_algorithm_;
		uint64_t pts_adjustment_;
		uint8_t cw_index_;
		uint32_t tier_;
		uint32_t splice_command_length_;
		uint8_t splice_command_type_;
		uint16_t descriptor_loop_length_;

		// information for the splice insert command
		uint32_t splice_insert_event_id_;
		bool splice_event_cancel_indicator_;

		void print(bool verbose = false);
		void parse(uint8_t *ptr, unsigned int size);

	};

	//--------------------------- fmp4 ingest stream definition --------------------------------------------
	struct init_fragment
	{
		box ftyp_box_;
		box moov_box_;

		uint32_t get_time_scale();
	};

	struct media_fragment
	{
		box styp_;
		box prft_;
		emsg emsg_;
		bool e_msg_is_in_mdat_;
		//
		box moof_box_;
		box mdat_box_;
		
		// as cmaf only has one traf box we directly store the entries of it
		mfhd mfhd_;
		tfhd tfhd_;
		tfdt tfdt_;
		trun trun_;

		// see what is in the fragment and store the sub boxes
		void parse_moof();
		void print();
		uint64_t get_duration();
	};

	struct ingest_stream
	{
		init_fragment init_fragment_;
		vector<media_fragment> media_fragment_;
		box sidx_box_, meta_box_, mfra_box_;
		int load_from_file(istream *input_file, bool init_only=false);
		int write_init_to_file(string &out_file);
		int write_to_sparse_emsg_file(string &out_file, uint32_t track_id, uint32_t announce, string &urn);
		uint64_t get_init_segment_data(vector<uint8_t> &init_seg_dat);
		uint64_t get_media_segment_data(long index, vector<uint8_t> &media_seg_dat);
		void write_to_dash_event_stream(string &out_file);
		void print();
	};

	//------------------ helpers for processing the bitstream ------------------------
	static uint16_t fmp4_endian_swap16(uint16_t in) {
		return ((in & 0x00FF) << 8) | ((in & 0xFF00) >> 8);
	};

	static uint32_t fmp4_endian_swap32(uint32_t in) {
		return  ((in & 0x000000FF) << 24) | \
			((in & 0x0000FF00) << 8) | \
			((in & 0x00FF0000) >> 8) | \
			((in & 0xFF000000) >> 24);
	};

	static uint64_t fmp4_endian_swap64(uint64_t in) {
		return  ((in & 0x00000000000000FF) << 56) | \
			((in & 0x000000000000FF00) << 40) | \
			((in & 0x0000000000FF0000) << 24) | \
			((in & 0x00000000FF000000) << 8) | \
			((in & 0x000000FF00000000) >> 8) | \
			((in & 0x0000FF0000000000) >> 24) | \
			((in & 0x00FF000000000000) >> 40) | \
			((in & 0xFF00000000000000) >> 56);
	};

	static uint16_t fmp4_read_uint16(char *pt)
	{
		return IS_BIG_ENDIAN ? *((uint16_t *)pt) : fmp4_endian_swap16(*((uint16_t *)pt));
	};

	static uint32_t fmp4_read_uint32(char *pt)
	{
		return IS_BIG_ENDIAN ? *((uint32_t *)pt) : fmp4_endian_swap32(*((uint32_t *)pt));
	};

	static uint64_t fmp4_read_uint64(char *pt)
	{
		return IS_BIG_ENDIAN ? *((uint64_t *)pt) : fmp4_endian_swap64(*((uint64_t *)pt));
	};

	static uint16_t fmp4_write_uint16(uint16_t in, char *pt)
	{
		return IS_BIG_ENDIAN ?  ((uint16_t *)pt)[0] = in : ((uint16_t *)pt)[0] = fmp4_endian_swap16(in);
	};

	static uint32_t fmp4_write_uint32(uint32_t in, char *pt)
	{
		return IS_BIG_ENDIAN ? ((uint32_t *)pt)[0] = in : ((uint32_t *)pt)[0] = fmp4_endian_swap32(in);
	};

	static uint64_t fmp4_write_uint64(uint64_t in,char *pt)
	{
		return IS_BIG_ENDIAN ? ((uint64_t *)pt)[0] = in : ((uint64_t *)pt)[0] = fmp4_endian_swap64(in);
	};

	
}
#endif