#ifndef ADVPLOTZOOMER_H
#define ADVPLOTZOOMER_H

#include <qwt_plot_zoomer.h>
#include <qwt_painter.h>
#include <qwt_picker_machine.h>
#include <qwt_plot_canvas.h>


/*!
  \brief AdvPlotZoomer extends QwtPlotZoomer by constrained zooms

  AdvPlotZoomer offers a new property zoomConstrain, which can have one of
  these values:

  - NoConstrain\n
    The original behavior of QwtPlot
  - HZoomOnly\n
    Only zooming the horizontal direction possible
  - VZoomOnly\n
    Only zooming the vertical direction possible

  The zoom rectangles for the constrained direction will always extend the
  full ZoomBase. Make sure the ZoomBase is set correctly.

  If the Rubberband style is set to RubberBand::RectRubberBand (the default),
  then draw only two instead of four lines.
*/
class AdvPlotZoomer : public QwtPlotZoomer
{
    Q_OBJECT

    Q_ENUMS( ZoomConstrain )

    Q_PROPERTY( ZoomConstrain zoomConstrain READ zoomConstrain \
                WRITE setZoomConstrain )

public:
    /*!
      Constrained zoom direction

      The default value is AdvPlotZoomer::NoConstrain.
      \sa setZoomConstrain(), zoomConstrain()
    */

    enum ZoomConstrain
    {
        //! No zooming constrain.
        NoConstrain = 0,

        //! Only horizontal zooming possible
        HZoomOnly,

        //! Only vertical zooming possible
        VZoomOnly,

        /*!
          Values >= UserConstrain can be used to define additional
          zooming constraints.
         */
        UserConstrain = 100
    };

    explicit AdvPlotZoomer( QwtPlotCanvas *canvas, bool doReplot = true );
    explicit AdvPlotZoomer( int xAxis, int yAxis, QwtPlotCanvas *canvas,
        bool doReplot = true );
    virtual ~AdvPlotZoomer();

    void drawRubberBand( QPainter *painter ) const;

    AdvPlotZoomer::ZoomConstrain zoomConstrain() const;
    void setZoomConstrain( ZoomConstrain zoomConstrain );

protected:
    virtual bool accept( QPolygon &pa ) const;

private:
    ZoomConstrain d_zoomConstrain;
};

#endif // ADVPLOTZOOMER_H
