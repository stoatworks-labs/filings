/**
 * Filings' two passes in WebGL2: the plugin's own GLSL, unedited.
 *
 * `shaders.js` is the five `R"( ... )"` pieces of `source/Shaders.cpp` plus
 * kVersion, spliced by `demo/tools/check_shaders.py --write` and never typed;
 * the same script compares them character for character, and checks they are
 * joined here as `AssembleVertex()`, `Assemble()` and InitGL join them in the
 * plugin.
 *
 *   1. needles    one instanced quad per filing (x, y, cos, sin as floats,
 *                 re-uploaded only when the sheet changed) into a transmission
 *                 buffer the size of the output, cleared to 1 and blended
 *                 multiplicatively (GL_ZERO, GL_SRC_COLOR), so it ends as the
 *                 product of ( 1 - coverage ) over every needle on a pixel.
 *   2. composite  the clip and the transmission to the output, in linear light:
 *                 out = clip x T + ink x ( 1 - T ), alpha = a + ( 1 - a )( 1 - T ).
 *
 * As in the plugin, the transmission is RGBA32F. Rendering into it needs
 * EXT_color_buffer_float (the kit asks for it and stops without it), and
 * blending into it needs EXT_float_blend, which desktop GL 4.1 has in core and
 * WebGL2 only as an extension. A browser without EXT_float_blend gets an
 * RGBA16F transmission buffer instead (which EXT_color_buffer_float makes
 * blendable), and the line under the picture says so: that is a difference in
 * the last bits of T, not in the model, and AGENTS.md says how big it was
 * measured to be.
 *
 * Everything the passes are handed comes from port.js's
 * FilingsPlugin.ProcessFrame: the instances and every uniform.
 */

import { Program, PassBuffer, bindTexture } from './vendor/gl.js';
import * as S from './shaders.js';

/** The needles' instance attribute, as InitGL sets it up: location 0, a vec4 an instance. */
const NEEDLE_ATTRIBS = { Filing: 0 };

export function createPasses(gl) {
  const floatBlend = gl.getExtension('EXT_float_blend') !== null;
  const transmissionFormat = floatBlend ? gl.RGBA32F : gl.RGBA16F;

  // InitGL: each pass is compiled from AssembleVertex( pass ) and
  // Assemble( pass ).
  const needleVertex = S.VERSION + S.NEEDLE_VERTEX;
  const quadVertex = S.VERSION + S.QUAD_VERTEX;
  const assemble = {
    needles: S.VERSION + S.NEEDLE_FRAGMENT,
    composite: S.VERSION + S.COMMON + S.COMPOSITE_FRAGMENT,
  };
  const needles = new Program(gl, needleVertex, assemble.needles, 'needles', { attribs: NEEDLE_ATTRIBS });
  const composite = new Program(gl, quadVertex, assemble.composite, 'composite', { attribs: {} });

  // The needles: one vertex array, its buffer stepping once an instance; and
  // an empty one for the composite's full-screen triangle (made from
  // gl_VertexID alone).
  const needleArray = gl.createVertexArray();
  const instanceBuffer = gl.createBuffer();
  gl.bindVertexArray(needleArray);
  gl.bindBuffer(gl.ARRAY_BUFFER, instanceBuffer);
  gl.enableVertexAttribArray(0);
  gl.vertexAttribPointer(0, 4, gl.FLOAT, false, 4 * 4, 0);
  gl.vertexAttribDivisor(0, 1);
  gl.bindVertexArray(null);
  gl.bindBuffer(gl.ARRAY_BUFFER, null);
  const emptyArray = gl.createVertexArray();

  const transmission = new PassBuffer(gl, { filter: 'nearest' });

  return {
    /** RGBA32F as in the plugin, or RGBA16F where the browser cannot blend into 32-bit floats. */
    floatBlend,

    /**
     * Draw one frame. `frame` is what FilingsPlugin.ProcessFrame returned;
     * `input` the clip ({ texture }, read with texelFetch at the output's own
     * pixel); the output goes to the canvas at width x height.
     */
    draw(frame, input, width, height) {
      transmission.ensure(width, height, transmissionFormat);

      // 1. The needles: transmissions multiplied into a buffer cleared to 1.
      gl.bindFramebuffer(gl.FRAMEBUFFER, transmission.fbo);
      gl.viewport(0, 0, width, height);
      gl.clearColor(1, 1, 1, 1);
      gl.clear(gl.COLOR_BUFFER_BIT);
      if (frame.count > 0) {
        gl.enable(gl.BLEND);
        gl.blendFunc(gl.ZERO, gl.SRC_COLOR);
        needles.use();
        needles.set('Centre', frame.centre[0], frame.centre[1]);
        needles.set('Scale', frame.scale);
        needles.set('Half', frame.half[0], frame.half[1]);
        needles.set('Size', frame.size[0], frame.size[1]);
        gl.bindVertexArray(needleArray);
        if (frame.rebuilt) {
          gl.bindBuffer(gl.ARRAY_BUFFER, instanceBuffer);
          gl.bufferData(gl.ARRAY_BUFFER, frame.instances, gl.DYNAMIC_DRAW);
          gl.bindBuffer(gl.ARRAY_BUFFER, null);
        }
        gl.drawArraysInstanced(gl.TRIANGLES, 0, 6, frame.count);
        gl.bindVertexArray(null);
        gl.disable(gl.BLEND);
      }

      // 2. The picture, to the canvas.
      gl.bindFramebuffer(gl.FRAMEBUFFER, null);
      gl.viewport(0, 0, width, height);
      composite.use();
      bindTexture(gl, 0, transmission.texture);
      bindTexture(gl, 1, input.texture);
      composite.setSampler('Transmission', 0);
      composite.setSampler('InputTexture', 1);
      gl.uniform2i(composite.location('ViewOrigin'), 0, 0);
      composite.set('Ink', frame.ink);
      composite.set('MixAmount', frame.mix);
      composite.setInt('Model', 0);
      gl.bindVertexArray(emptyArray);
      gl.drawArrays(gl.TRIANGLES, 0, 3);
      gl.bindVertexArray(null);
      bindTexture(gl, 1, null);
      bindTexture(gl, 0, null);
    },

    /** Uniform names a program was asked for and does not have (should stay empty). */
    missing() {
      return [...needles.missing, ...composite.missing];
    },
  };
}
