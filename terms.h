#pragma once
#include <switch.h>
#include <switch_mallocators.h>
#include <switch_vector.h>
#include <compress.h>
#include "codecs.h"


// prefic compressed terms dictionary
// maps from strwlen8_t=>term_index_ctx
// Based in part on Lucene's prefix compression scheme
// https://lucene.apache.org/core/2_9_4/fileformats.html#Term%20Dictionary
namespace Trinity
{
	struct terms_skiplist_entry
        {
                strwlen8_t term;
                uint32_t blockOffset; 	// offset in the terms datafile
                term_index_ctx tctx;	// payload
        };

        term_index_ctx lookup_term(range_base<const uint8_t *, uint32_t> termsData, const strwlen8_t term, const Switch::vector<terms_skiplist_entry> &skipList);

        void unpack_terms_skiplist(const range_base<const uint8_t *, const uint32_t> termsIndex, Switch::vector<terms_skiplist_entry> *skipList, simple_allocator &allocator);

        void pack_terms(std::vector<std::pair<strwlen8_t, term_index_ctx>> &terms, IOBuffer *const data, IOBuffer *const index);

	// An abstract index source terms access wrapper
	// For segments, you will likely use the prefix-compressed terms infra. but you may have
	// an index source that is e.g storing all those terms in an in-memory std::unordered_map<> or whatever else
	// for some reason and you can just write an IndexSourceTermsView subclass to access that.
	//
	// IndexSourceTermsView subclasses are used while merging index sources
	//
	// see merge.h
	struct IndexSourceTermsView
	{
		virtual std::pair<strwlen8_t, term_index_ctx> cur() = 0;

		virtual void next() = 0;

		virtual bool done() = 0;
	};


	// iterator access to the terms data
	// this is very useful for merging terms dictionaries (see IndexSourcePrefixCompressedTermsView)
	struct terms_data_view
        {
              public:
                struct iterator
                {
                        friend struct terms_data_view;

                      public:
                        const uint8_t *p;
                        char termStorage[256];
                        struct
                        {
                                strwlen8_t term;
                                term_index_ctx tctx;
                        } cur;

                        iterator(const uint8_t *ptr)
                            : p{ptr}
                        {
                                cur.term.p = termStorage;
                        }

                        inline bool operator==(const iterator &o) const noexcept
                        {
                                return p == o.p;
                        }

                        inline bool operator!=(const iterator &o) const noexcept
                        {
                                return p != o.p;
                        }

                        strwlen8_t term() noexcept
                        {
                                decode_cur();
                                return cur.term;
                        }

                        term_index_ctx tctx() noexcept
                        {
                                decode_cur();
                                return cur.tctx;
                        }

                        inline iterator &operator++()
                        {
                                cur.term.len = 0;
                                return *this;
                        }

                        inline std::pair<strwlen8_t, term_index_ctx> operator*() noexcept
                        {
                                decode_cur();
                                return {cur.term, cur.tctx};
                        }

                      protected:
                        void decode_cur();
                };

              private:
                const range_base<const uint8_t *, uint32_t> termsData;

              public:
                iterator begin() const
                {
                        return {termsData.start()};
                }

                iterator end() const
                {
                        return {termsData.stop()};
                }

                terms_data_view(const range_base<const uint8_t *, uint32_t> d)
                    : termsData{d}
                {
                }
        };

	// A specialised IndexSourceTermsView for accessing prefix-encoded terms dictionaries
	struct IndexSourcePrefixCompressedTermsView
		: public IndexSourceTermsView
        {
              private:
                terms_data_view::iterator it, end;

              public:
                IndexSourcePrefixCompressedTermsView(const range_base<const uint8_t *, uint32_t> termsData)
                    : it{termsData.start()}, end{termsData.stop()}
                {
                }

                std::pair<strwlen8_t, term_index_ctx> cur() override final
                {
                        return *it;
                }

                void next() override final
                {
                        ++it;
                }

                bool done() override final
                {
                        return it == end;
                }
        };

        class SegmentTerms
        {
		private:
			Switch::vector<terms_skiplist_entry> skiplist;
			simple_allocator allocator;
			range_base<const uint8_t *, uint32_t> termsData;


                      public:
			SegmentTerms(const char *segmentBasePath);

			~SegmentTerms()
			{
                                if (auto ptr = (void *)(termsData.offset))
                                        munmap(ptr, termsData.size());
			}

			term_index_ctx lookup(const strwlen8_t term)
			{
				return lookup_term(termsData, term, skiplist);
			}

			auto terms_data_access() const
			{ 
				return terms_data_view(termsData);
			}
	};

};