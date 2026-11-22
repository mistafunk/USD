// About dialog (D27): what the demonstrator tries out and a diagram of how
// it places models. Opened by the header's i button, the title, or #about in
// the URL; closed by Esc, the close button or a click on the backdrop. The
// diagrams are their own files (about-diagram.svg: the pipeline;
// about-mapping.svg: a 3D object feature and the geospatial CRS schema),
// inserted inline so they use the page's colour variables.
import diagram from "./about-diagram.svg?raw";
import mapping from "./about-mapping.svg?raw";

export function initAbout() {
  const dialog = document.getElementById("about") as HTMLDialogElement;
  document.getElementById("aboutDiagram")!.innerHTML = diagram;
  document.getElementById("aboutMapping")!.innerHTML = mapping;
  const open = () => { if (!dialog.open) dialog.showModal(); };
  document.getElementById("aboutOpen")!.addEventListener("click", open);
  document.getElementById("aboutTitle")!.addEventListener("click", open);
  document.getElementById("aboutClose")!.addEventListener("click", () => dialog.close());
  // A click on the backdrop lands on the dialog element itself.
  dialog.addEventListener("click", (e) => { if (e.target === dialog) dialog.close(); });
  if (location.hash === "#about") open();
}
