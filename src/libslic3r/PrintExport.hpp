#ifndef PRINTEXPORT_HPP
#define PRINTEXPORT_HPP

// For png export of the sliced model
#include <fstream>
#include <sstream>
#include <vector>

#include <boost/log/trivial.hpp>

#include "Rasterizer/Rasterizer.hpp"
//#include <tbb/parallel_for.h>
//#include <tbb/spin_mutex.h>//#include "tbb/mutex.h"

namespace Slic3r {

// Used for addressing parameters of FilePrinter::set_statistics()
enum ePrintStatistics
{
    psUsedMaterial = 0,
    psNumFade,
    psNumSlow,
    psNumFast,

    psCnt
};

enum class FilePrinterFormat {
    SLA_PNGZIP,
    SVG
};

/*
 * Interface for a file printer of the slices. Implementation can be an SVG
 * or PNG printer or any other format.
 *
 * The format argument specifies the output format of the printer and it enables
 * different implementations of this class template for each supported format.
 *
 */
template<FilePrinterFormat format>
class FilePrinter {
public:

    // Draw an ExPolygon which is a polygon inside a slice on the specified layer.
    void draw_polygon(const ExPolygon& p, unsigned lyr);

    // Tell the printer how many layers should it consider.
    void layers(unsigned layernum);

    // Get the number of layers in the print.
    unsigned layers() const;

    /* Switch to a particular layer. If there where less layers then the
     * specified layer number than an appropriate number of layers will be
     * allocated in the printer.
     */
    void begin_layer(unsigned layer);

    // Allocate a new layer on top of the last and switch to it.
    void begin_layer();

    /*
     * Finish the selected layer. It means that no drawing is allowed on that
     * layer anymore. This fact can be used to prepare the file system output
     * data like png comprimation and so on.
     */
    void finish_layer(unsigned layer);

    // Finish the top layer.
    void finish_layer();

    // Save all the layers into the file (or dir) specified in the path argument
    void save(const std::string& path);

    // Save only the selected layer to the file specified in path argument.
    void save_layer(unsigned lyr, const std::string& path);
};

// Provokes static_assert in the right way.
template<class T = void> struct VeryFalse { static const bool value = false; };

// This has to be explicitly implemented in the gui layer or a default zlib
// based implementation is needed. I don't have time for that and I'm delegating
// the implementation to the gui layer where the gui toolkit can cover this.
template<class Fmt> class LayerWriter {
public:

    LayerWriter(const std::string& /*zipfile_path*/) {
        static_assert(VeryFalse<Fmt>::value,
                      "No layer writer implementation provided!");
    }

    void next_entry(const std::string& /*fname*/) {}

    std::string get_name() { return ""; }

    bool is_ok() { return false; }

    template<class T> LayerWriter& operator<<(const T& /*arg*/) {
        return *this;
    }

    void close() {}
};

// Implementation for PNG raster output
// Be aware that if a large number of layers are allocated, it can very well
// exhaust the available memory especially on 32 bit platform.
template<> class FilePrinter<FilePrinterFormat::SLA_PNGZIP>
{
    struct Layer {
        Raster first;
        std::stringstream second;

        Layer() {}

        Layer(const Layer&) = delete;
        Layer(Layer&& m):
            first(std::move(m.first))/*, second(std::move(m.second))*/ {}
    };

    // We will save the compressed PNG data into stringstreams which can be done
    // in parallel. Later we can write every layer to the disk sequentially.
    std::vector<Layer> m_layers_rst;
    Raster::Resolution m_res;
    Raster::PixelDim m_pxdim;
    double m_exp_time_s = .0, m_exp_time_first_s = .0;
    double m_layer_height = .0;
    Raster::Origin m_o = Raster::Origin::TOP_LEFT;

    double m_used_material = 0.0;
    int    m_cnt_fade_layers = 0;
    int    m_cnt_slow_layers = 0;
    int    m_cnt_fast_layers = 0;

    std::string createIniContent(const std::string& projectname) {
//         double layer_height = m_layer_height;

        using std::string;
        using std::to_string;

        auto expt_str = to_string(m_exp_time_s);
        auto expt_first_str = to_string(m_exp_time_first_s);
//         auto stepnum_str = to_string(static_cast<unsigned>(800*layer_height));
        auto layerh_str = to_string(m_layer_height);

        const std::string cnt_fade_layers = to_string(m_cnt_fade_layers);
        const std::string cnt_slow_layers = to_string(m_cnt_slow_layers);
        const std::string cnt_fast_layers = to_string(m_cnt_fast_layers);
        const std::string used_material   = to_string(m_used_material);

        return string(
        "action = print\n"
        "jobDir = ") + projectname + "\n" +
        "expTime = " + expt_str + "\n"
        "expTimeFirst = " + expt_first_str + "\n"
        "numFade = " + cnt_fade_layers + "\n"
        "layerHeight = " + layerh_str + "\n"
        "usedMaterial = " + used_material + "\n"
        "numSlow = " + cnt_slow_layers + "\n"
        "numFast = " + cnt_fast_layers + "\n";
    }

public:

    enum RasterOrientation {
        RO_LANDSCAPE,
        RO_PORTRAIT
    };

    // We will play with the raster's coordinate origin parameter. When the
    // printer should print in landscape mode it should have the Y axis flipped
    // because the layers should be displayed upside down. PNG has its
    // coordinate origin in the top-left corner so normally the Raster objects
    // should be instantiated with the TOP_LEFT flag. However, in landscape mode
    // we do want the pictures to be upside down so we will make BOTTOM_LEFT
    // type rasters and the PNG format will do the flipping automatically.

    // In case of portrait images, we have to rotate the image by a 90 degrees
    // and flip the y axis. To get the correct upside-down orientation of the
    // slice images, we can flip the x and y coordinates of the input polygons
    // and do the Y flipping of the image. This will generate the correct
    // orientation in portrait mode.

    inline FilePrinter(double width_mm, double height_mm,
                       unsigned width_px, unsigned height_px,
                       double layer_height,
                       double exp_time, double exp_time_first,
                       RasterOrientation ro = RO_PORTRAIT):
        m_res(width_px, height_px),
        m_pxdim(width_mm/width_px, height_mm/height_px),
        m_exp_time_s(exp_time),
        m_exp_time_first_s(exp_time_first),
        m_layer_height(layer_height),

        // Here is the trick with the orientation.
        m_o(ro == RO_LANDSCAPE? Raster::Origin::BOTTOM_LEFT :
                                Raster::Origin::TOP_LEFT )
    {
    }

    FilePrinter(const FilePrinter& ) = delete;
    FilePrinter(FilePrinter&& m):
        m_layers_rst(std::move(m.m_layers_rst)),
        m_res(m.m_res),
        m_pxdim(m.m_pxdim) {}

    inline void layers(unsigned cnt) { if(cnt > 0) m_layers_rst.resize(cnt); }
    inline unsigned layers() const { return unsigned(m_layers_rst.size()); }

    inline void draw_polygon(const ExPolygon& p, unsigned lyr) {
        assert(lyr < m_layers_rst.size());
        m_layers_rst[lyr].first.draw(p);
    }

    inline void begin_layer(unsigned lyr) {
        if(m_layers_rst.size() <= lyr) m_layers_rst.resize(lyr+1);
        m_layers_rst[lyr].first.reset(m_res, m_pxdim, m_o);
    }

    inline void begin_layer() {
        m_layers_rst.emplace_back();
        m_layers_rst.front().first.reset(m_res, m_pxdim, m_o);
    }

    inline void finish_layer(unsigned lyr_id) {
        assert(lyr_id < m_layers_rst.size());
        m_layers_rst[lyr_id].first.save(m_layers_rst[lyr_id].second,
                                       Raster::Compression::PNG);
        m_layers_rst[lyr_id].first.reset();
    }

    inline void finish_layer() {
        if(!m_layers_rst.empty()) {
            m_layers_rst.back().first.save(m_layers_rst.back().second,
                                          Raster::Compression::PNG);
            m_layers_rst.back().first.reset();
        }
    }

    template<class LyrFmt>
    inline void save(const std::string& path) {
        try {
            LayerWriter<LyrFmt> writer(path);
            if(!writer.is_ok()) return;

            std::string project = writer.get_name();

            writer.next_entry("config.ini");
            if(!writer.is_ok()) return;

            writer << createIniContent(project);

            for(unsigned i = 0; i < m_layers_rst.size() && writer.is_ok(); i++)
            {
                if(m_layers_rst[i].second.rdbuf()->in_avail() > 0) {
                    char lyrnum[6];
                    std::sprintf(lyrnum, "%.5d", i);
                    auto zfilename = project + lyrnum + ".png";
                    writer.next_entry(zfilename);

                    if(!writer.is_ok()) break;

                    writer << m_layers_rst[i].second.str();
                    // writer << m_layers_rst[i].second.rdbuf();
                    // we can keep the date for later calls of this method
                    //m_layers_rst[i].second.str("");
                }
            }
        } catch(std::exception& e) {
            BOOST_LOG_TRIVIAL(error) << e.what();
            // Rethrow the exception
            throw;
        }
    }

    void save_layer(unsigned lyr, const std::string& path) {
        unsigned i = lyr;
        assert(i < m_layers_rst.size());

        char lyrnum[6];
        std::sprintf(lyrnum, "%.5d", lyr);
        std::string loc = path + "layer" + lyrnum + ".png";

        std::fstream out(loc, std::fstream::out | std::fstream::binary);
        if(out.good()) {
            m_layers_rst[i].first.save(out, Raster::Compression::PNG);
        } else {
            BOOST_LOG_TRIVIAL(error) << "Can't create file for layer";
        }

        out.close();
        m_layers_rst[i].first.reset();
    }

    void set_statistics(const std::vector<double> statistics)
    {
        if (statistics.size() != psCnt)
            return;

        m_used_material   = statistics[psUsedMaterial];
        m_cnt_fade_layers = int(statistics[psNumFade]);
        m_cnt_slow_layers = int(statistics[psNumSlow]);
        m_cnt_fast_layers = int(statistics[psNumFast]);
    }
};

}

#endif // PRINTEXPORT_HPP
