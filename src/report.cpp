#include "analyzer/benchmark.hpp"
#include "analyzer/text.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace analyzer {
namespace {
std::string csv_quote(std::string_view value) {
    std::string output = "\"";
    for (const char c : value) { if (c == '"') output += '"'; output += c; }
    return output + '"';
}
std::string md_escape(std::string_view value) {
    std::string output;
    for (const char c : value) {
        if (c == '|' || c == '\\' || c == '`' || c == '<' || c == '>') output += '\\';
        if (c == '\n' || c == '\r') { output += ' '; continue; }
        output += c;
    }
    return output;
}
void prepare_output(const std::filesystem::path& path, const std::filesystem::path& input) {
    if (same_file(path, input)) throw std::runtime_error("Report output must not overwrite the input file");
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
}
void finish(std::ofstream& stream) {
    stream.close();
    if (!stream) throw std::runtime_error("Report write failed");
}
}

void print_comparison(const BenchmarkResult& result) {
    const auto& r = result.reference;
    std::cout << "\nAnalysis: " << r.bytes << " bytes | " << r.lines << " lines | " << r.words
              << " words | ERROR=" << r.errors << " WARN=" << r.warnings << " INFO=" << r.infos << "\n\n";
    std::cout << std::left << std::setw(19) << "Method" << std::right << std::setw(10) << "Req/used"
              << std::setw(12) << "Median(s)" << std::setw(12) << "IQR(s)" << std::setw(12) << "MiB/s"
              << std::setw(10) << "Speedup" << std::setw(12) << "Peak MiB" << "  Match\n";
    std::cout << std::string(97, '-') << '\n';
    for (const auto& s : result.summaries) {
        std::cout << std::left << std::setw(19) << mode_name(s.configuration.mode) << std::right
                  << std::setw(10) << (std::to_string(s.configuration.threads) + "/" + std::to_string(s.actual_workers))
                  << std::fixed << std::setprecision(6) << std::setw(12) << s.median_seconds << std::setw(12) << s.iqr_seconds
                  << std::setprecision(2) << std::setw(12) << s.mib_per_second << std::setw(9) << s.speedup << 'x'
                  << std::setw(12) << s.median_peak_mib << "  yes\n";
    }
    std::cout << "\nSpeedup is relative to buffered. Peak MiB is the median lifetime peak working set\n"
                 "of fresh trial processes, including mapped resident pages. Cache was pre-read\n"
                 "before every trial; this is not a cold-cache benchmark.\n";
}

void write_reports(const BenchmarkOptions& options, const BenchmarkResult& result) {
    auto summary_path = options.csv_output;
    summary_path.replace_extension(".summary.csv");
    auto markdown_path = options.csv_output;
    markdown_path.replace_extension(".md");
    auto metadata_path = options.csv_output;
    metadata_path.replace_extension(".metadata.txt");
    for (const auto& path : {options.csv_output, summary_path, markdown_path, metadata_path}) prepare_output(path, options.input);

    std::ofstream csv(options.csv_output, std::ios::binary);
    if (!csv) throw std::runtime_error("Cannot create raw CSV report");
    csv << "input,round,method,requested_workers,actual_workers,seconds,cpu_seconds,peak_working_set_bytes,page_faults,bytes,lines,words,ERROR,WARN,INFO,results_match,cache_policy\n";
    for (const auto& trial : result.trials) {
        const auto& measurement = trial.measurement;
        const auto& r = measurement.analysis.result;
        const auto& m = measurement.metrics;
        csv << csv_quote(path_text(std::filesystem::absolute(options.input))) << ',' << trial.round << ','
            << mode_name(trial.configuration.mode) << ',' << trial.configuration.threads << ',' << measurement.analysis.workers << ','
            << std::setprecision(17) << m.seconds << ',' << m.cpu_seconds << ',' << m.peak_resident_bytes << ',' << m.page_faults << ','
            << r.bytes << ',' << r.lines << ',' << r.words << ',' << r.errors << ',' << r.warnings << ',' << r.infos
            << ",true,pre-read-residency-unverified\n";
    }
    finish(csv);
    std::ofstream summaries(summary_path, std::ios::binary);
    if (!summaries) throw std::runtime_error("Cannot create summary CSV");
    summaries << "method,requested_workers,actual_workers,median_seconds,iqr_seconds,throughput_mib_s,speedup_vs_buffered,scaling_vs_mapped,time_reduction_percent,median_cpu_seconds,median_peak_working_set_mib,median_page_faults\n";
    for (const auto& s : result.summaries) {
        summaries << mode_name(s.configuration.mode) << ',' << s.configuration.threads << ',' << s.actual_workers << ','
                  << std::setprecision(17) << s.median_seconds << ',' << s.iqr_seconds << ',' << s.mib_per_second << ',' << s.speedup << ',';
        if (s.configuration.mode != Mode::buffered) summaries << s.mapped_scaling;
        summaries << ',' << 100 * (1 - 1 / s.speedup) << ',' << s.median_cpu_seconds << ',' << s.median_peak_mib << ',' << s.median_faults << '\n';
    }
    finish(summaries);
    std::ofstream metadata_file(metadata_path, std::ios::binary);
    if (!metadata_file) throw std::runtime_error("Cannot create metadata report");
    for (const auto& [key, value] : result.metadata) metadata_file << key << '=' << value << '\n';
    finish(metadata_file);

    std::ofstream markdown(markdown_path, std::ios::binary);
    if (!markdown) throw std::runtime_error("Cannot create comparison report");
    const auto& r = result.reference;
    markdown << "# File analyzer comparison\n\nInput: " << md_escape(path_text(std::filesystem::absolute(options.input)))
             << "\n\n" << r.bytes << " bytes; " << r.lines << " lines; " << r.words << " words. "
             << "ERROR: " << r.errors << "; WARN: " << r.warnings << "; INFO: " << r.infos << ".\n\n"
             << "Every timed run returned the same full set of counts as the buffered reference.\n\n"
             << "| Method | Requested/used workers | Median (s) | IQR (s) | MiB/s | Speedup vs buffered | Scaling vs mapped | Median peak working set (MiB) |\n"
             << "| --- | --- | --- | --- | --- | --- | --- | --- |\n";
    for (const auto& s : result.summaries) {
        markdown << "| " << mode_name(s.configuration.mode) << " | " << s.configuration.threads << '/' << s.actual_workers
                 << " | " << std::fixed << std::setprecision(6) << s.median_seconds << " | " << s.iqr_seconds
                 << " | " << std::setprecision(2) << s.mib_per_second << " | " << s.speedup << "x | ";
        if (s.configuration.mode == Mode::buffered) markdown << "n/a";
        else markdown << s.mapped_scaling << 'x';
        markdown << " | " << s.median_peak_mib << " |\n";
    }
    markdown << "\n## Protocol and interpretation\n\n"
             << "Each configuration ran " << options.runs << " times in fresh processes. Configurations were shuffled within each round. "
             << "The file was completely pre-read outside the trial process before each run; cache residency is not guaranteed. "
             << "This experiment does not claim cold-cache or raw disk throughput.\n\n"
             << "Timing includes file validation/open, mapping, partitioning, worker startup/join, merge, and close/unmap. "
             << "It excludes process startup, pre-read, metrics queries, and report output. IQR uses linearly interpolated 25th and 75th percentiles.\n\n"
             << "Peak working set is a lifetime maximum per fresh process, including its runtime baseline and resident mapped pages; "
             << "it is not heap allocation or incremental memory above baseline. Page faults combine soft and hard faults. "
             << "CPU timing may be coarse for short trials. Speedup below 1 means the method was slower than buffered reading.\n\n"
             << "Line-aligned partitioning may use fewer workers for very long or very few lines. "
             << "These measurements apply to this dataset and machine; they do not establish that mapping is always faster.\n\n"
             << "## Environment\n\n| Field | Value |\n| --- | --- |\n";
    for (const auto& [key, value] : result.metadata) markdown << "| " << key << " | " << md_escape(value) << " |\n";
    finish(markdown);
}
}
