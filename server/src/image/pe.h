#pragma once

// Had to put implementation inside a header file because im using templated
// class to make it work in its separate source file, I had to do some weird
// fuckery but I didn't want to do it

namespace pe {

struct import_t {
  std::string name;
  uint32_t rva;
};

struct section_t {
  std::string name;
  size_t size;
  uint32_t rva;
  uint32_t va;
};

template <bool x64 = false>
class image {
  win::image_t<x64> *m_image;
  std::vector<char> m_buffer;

  std::unordered_map<std::string, std::vector<import_t>> m_imports;
  std::vector<section_t> m_sections;
  std::vector<std::pair<uint32_t, win::reloc_entry_t>> m_relocs;

 public:
  image(const std::string_view name) : m_image{nullptr} {
    io::read_file(name, m_buffer);
    if (m_buffer.empty()) {
      io::logger->error("failed to read image {}", name);
      return;
    }

    m_image = reinterpret_cast<win::image_t<x64> *>(m_buffer.data());
    load();
  }

  void load() {
    parse_sections();
    parse_relocs();
    parse_imports();
  }

  void reload(const std::string_view name) {
    io::read_file(name, m_buffer);
    if (m_buffer.empty()) {
      io::logger->error("failed to reload image {}", name);
      return;
    }

    m_image = reinterpret_cast<win::image_t<x64> *>(m_buffer.data());
    load();
  }

  void parse_sections() {
    const auto nt = m_image->get_nt_headers();
    const size_t n = nt->file_header.num_sections;

    for (size_t i = 0; i < n; i++) {
      auto section = nt->get_section(i);
      m_sections.emplace_back(section_t{section->name, section->size_raw_data,
                               section->ptr_raw_data,
                               section->virtual_address});
    }
  };

  void parse_relocs() {
    const auto reloc_dir =
        m_image->get_directory(win::directory_id::directory_entry_basereloc);
    if (!reloc_dir) return;

    const auto ptr = m_image->rva_to_ptr(reloc_dir->rva);
    auto block = reinterpret_cast<win::reloc_block_t *>(ptr);

    while (block->base_rva) {
      for (size_t i = 0; i < block->num_entries(); ++i) {
        auto entry = block->entries[i];

        m_relocs.emplace_back(std::make_pair(block->base_rva, entry));
      }
      block = block->get_next();
    }
  }

  void parse_imports() {
    const auto import_dir =
        m_image->get_directory(win::directory_id::directory_entry_import);
    if (!import_dir) return;

    const auto ptr = m_image->rva_to_ptr(import_dir->rva);
    auto table = reinterpret_cast<win::import_directory_t *>(ptr);

    for (uint32_t previous_name = 0; previous_name < table->rva_name;
         previous_name = table->rva_name, ++table) {
      auto name_ptr = m_image->rva_to_ptr(table->rva_name);
      auto mod_name = std::string(reinterpret_cast<char *>(name_ptr));

      auto thunk = reinterpret_cast<win::image_thunk_data_t<x64> *>(
          m_image->rva_to_ptr(table->rva_original_first_thunk));

      for (uint32_t index = 0; thunk->address;
           index += sizeof(uint32_t), ++thunk) {
        auto named_import = reinterpret_cast<win::image_named_import_t *>(
            m_image->rva_to_ptr(thunk->address));

        if (!thunk->is_ordinal) {
          import_t data;
          data.name = reinterpret_cast<const char *>(named_import->name);
          data.rva = table->rva_first_thunk + index;

          m_imports[mod_name].emplace_back(std::move(data));
        }
      }
    }
  }

  const auto operator()() { return m_image; }
  operator bool() const { return m_image != nullptr; }

  auto imports() const { return m_imports; }
  auto relocs() const { return m_relocs; }
  auto sections() const { return m_sections; }

  std::string get_json_imports() {
    nlohmann::json json;
    for(auto &[mod, imports] : m_imports) {
      for(auto &i : imports) {
        json[mod].emplace_back(std::make_pair(i.name, i.rva));
      }
    }
    return json.dump();
  }
};

};  // namespace pe