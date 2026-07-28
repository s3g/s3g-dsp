(() => {
  const triggers = Array.from(document.querySelectorAll("[data-lightbox-image]"));
  if (!triggers.length) return;

  let lightbox = document.querySelector("[data-page-lightbox]");
  if (!lightbox) {
    lightbox = document.createElement("div");
    lightbox.className = "media-lightbox";
    lightbox.dataset.pageLightbox = "";
    lightbox.setAttribute("aria-hidden", "true");
    document.body.appendChild(lightbox);
  }
  lightbox.setAttribute("role", "dialog");
  lightbox.setAttribute("aria-modal", "true");
  lightbox.setAttribute("aria-label", "Plugin GUI screenshot");

  let close = lightbox.querySelector("[data-page-lightbox-close]");
  if (!close) {
    close = document.createElement("button");
    close.className = "media-lightbox-close";
    close.type = "button";
    close.dataset.pageLightboxClose = "";
    close.setAttribute("aria-label", "Close image");
    close.textContent = "x";
    lightbox.appendChild(close);
  }

  let image = lightbox.querySelector("[data-page-lightbox-image]");
  if (!image) {
    image = document.createElement("img");
    image.dataset.pageLightboxImage = "";
    image.alt = "";
    lightbox.appendChild(image);
  }

  let activeTrigger = null;

  function show(trigger) {
    const img = trigger.querySelector("img");
    activeTrigger = trigger;
    image.src = trigger.dataset.lightboxImage;
    image.alt = img ? img.alt : "";
    lightbox.setAttribute("aria-hidden", "false");
    document.body.classList.add("media-lightbox-open");
    close.focus();
  }

  function hide() {
    lightbox.setAttribute("aria-hidden", "true");
    document.body.classList.remove("media-lightbox-open");
    image.src = "";
    if (activeTrigger) activeTrigger.focus();
    activeTrigger = null;
  }

  triggers.forEach(trigger => trigger.addEventListener("click", () => show(trigger)));
  close.addEventListener("click", hide);
  lightbox.addEventListener("click", event => {
    if (event.target === lightbox) hide();
  });
  document.addEventListener("keydown", event => {
    if (lightbox.getAttribute("aria-hidden") === "true") return;
    if (event.key === "Escape") hide();
  });
})();
