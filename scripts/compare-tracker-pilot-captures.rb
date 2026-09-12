#!/usr/bin/env ruby
# Compare paired captures from S3G_TRACKER_PILOT_CAPTURE_DIR. Read-only inputs;
# temporary raster files are automatically cleaned up. Requires Poppler and
# ImageMagick. No running DAW or installed plug-in is touched.
require 'json'
require 'open3'
require 'tmpdir'

abort 'usage: compare-tracker-pilot-captures.rb COCOA_DIR PILOT_DIR' unless ARGV.size == 2
reference, pilot = ARGV.map { |path| File.expand_path(path) }

def command(*args)
  output, error, status = Open3.capture3(*args)
  abort "#{args.first} failed: #{error}" unless status.success?
  output
end

def words(path)
  command('pdftotext', '-bbox', path, '-').scan(
    /<word xMin="([^"]+)" yMin="([^"]+)" xMax="([^"]+)" yMax="([^"]+)">([^<]+)<\/word>/
  ).map { |x, y, right, bottom, text| [text, *[x, y, right, bottom].map(&:to_f)] }
   .sort_by { |word| [word[0], word[1].round(2), word[2].round(2)] }
end

def fonts(path)
  command('pdffonts', path).lines.drop(2).map do |line|
    line.split.first.sub(/^[A-Z]{6}\+/, '')
  end.uniq.sort
end

files = Dir.glob(File.join(reference, '*.pdf')).sort
abort 'No reference PDFs found' if files.empty?
results = []
Dir.mktmpdir('tracker-parity-') do |temp|
  files.each do |source|
    name = File.basename(source)
    target = File.join(pilot, name)
    abort "Missing pilot capture: #{name}" unless File.file?(target)
    expected, actual = [source, target].map { |path| words(path) }
    viewport = nil
    if File.file?(source + '.json') && File.file?(target + '.json')
      viewport = JSON.parse(File.read(source + '.json')).fetch('trackerViewport')
      pilot_viewport = JSON.parse(File.read(target + '.json')).fetch('trackerViewport')
      abort "Viewport geometry changed: #{name}" unless viewport.zip(pilot_viewport)
        .all? { |x, y| (x - y).abs <= 0.01 }
      x, y, width, height = viewport
      # Poppler reports glyphs even when a PDF clipping path hides them. Only
      # compare grid text intersecting the recorded viewport. The raster metric
      # below still compares the entire page, including all native controls.
      visible = ->(word) { word[3] > x && word[1] < x + width &&
        word[4] > y && word[2] < y + height }
      expected = expected.select(&visible)
      actual = actual.select(&visible)
    end
    positions_match = expected.size == actual.size && expected.zip(actual).all? do |a, b|
      a[0] == b[0] && a.drop(1).zip(b.drop(1)).all? { |x, y| (x - y).abs <= 0.01 }
    end
    same_fonts = fonts(source) == fonts(target)
    [source, target].each_with_index do |path, i|
      command('pdftocairo', '-png', '-singlefile', '-r', '72', path,
        File.join(temp, i.to_s))
    end
    _, metric, status = Open3.capture3('magick', 'compare', '-metric', 'RMSE',
      File.join(temp, '0.png'), File.join(temp, '1.png'), 'null:')
    abort "Image comparison failed: #{metric}" unless [0, 1].include?(status.exitstatus)
    normalized_rmse = Float(metric[/\(([^)]+)\)/, 1])
    results << {
      capture: name, text_viewport: viewport,
      reference_words: expected.size, pilot_words: actual.size,
      text_positions_match: positions_match, font_faces_match: same_fonts,
      normalized_pixel_rmse: normalized_rmse,
      # Allows minor 8-bit color quantization / vector antialiasing differences,
      # not missing text or changed geometry. Inspect the paired images too.
      passed: positions_match && same_fonts && normalized_rmse <= 0.01
    }
  end
end
puts JSON.pretty_generate(results)
exit(results.all? { |result| result[:passed] } ? 0 : 1)
